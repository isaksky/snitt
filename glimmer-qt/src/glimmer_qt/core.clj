(ns glimmer-qt.core
  "Qt Quick backend with explicit root and session ownership. Requiring installs it.
  Use glimmer.core/run and reload!; each run owns one window and a native arena."
  (:require [glimmer.backend :as b]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]
            [jolt.ffi :as ffi]
            [jolt.host :as host]))

(defonce ^:private jobs (atom {:session nil :work {}}))
(defonce ^:private next-job (atom 0))

(defn later!
  "Run once on the GUI thread. Returns a token for cancel!, or nil after close.
  Safe from a worker. Jobs belong to the current session and never survive it."
  [delay-ms work]
  (when-not (and (integer? delay-ms) (<= 0 delay-ms 2147483647) (ifn? work))
    (throw (ex-info "Timer requires a callable and a nonnegative int32 delay." {})))
  (when-let [session (:session @jobs)]
    (let [token (swap! next-job inc)
          state (swap! jobs #(if (= session (:session %)) (assoc-in % [:work token] work) %))]
      (when (= session (:session state))
        (if (zero? (q/post session token delay-ms))
          (do (swap! jobs update :work dissoc token) nil)
          token)))))

(defn cancel!
  "Drop a scheduled thunk immediately. A queued native timer then becomes inert."
  [token]
  (swap! jobs update :work dissoc token)
  nil)

(defn pending-count [] (count (:work @jobs)))

(defn quit! []
  (when-let [session @w/session] (q/exit session 0))
  nil)

(defn- dispatch! [widget event value]
  (if (= event "work")
    (let [token (parse-long value)
          [before _] (swap-vals! jobs update :work dissoc token)]
      (when-let [work (get-in before [:work token])] (work)))
    (w/dispatch! widget event value)))

(defn- run-session! [opts {:keys [mount! unmount! abandon!]}]
  (let [{:keys [title width height app-id on-ready auto-quit-ms]
         :or {title "Glimmer · Qt" width 560 height 360 app-id "glimmer.qt"}} opts
        session (q/open title width height app-id)
        error (atom nil)
        callback (atom nil)]
    (try
      (reset! w/session session)
      (reset! jobs {:session session :work {}})
      ;; Set before mount so worker mutations cannot touch an incomplete tree.
      (reset! b/loop-running? true)
      (let [window (w/register-window! (q/container session))]
        (reset! callback
                (ffi/foreign-callable
                 (fn [widget event value]
                   (when-not @error
                     (try (dispatch! widget event value)
                          (catch Throwable e
                             ;; Never unwind managed exceptions through C++.
                             ;; Stop subscriptions immediately, even for a failure
                             ;; inside a control's signal handler.
                            (reset! error e)
                            (try (abandon!) (catch Throwable _ nil))
                            (q/exit session 1)))))
                 [:uint64 :string :string] :void :collect-safe))
        (q/set-handler session @callback)
        (mount! window :window)
        (when on-ready (later! 0 on-ready))
        (when auto-quit-ms (later! auto-quit-ms quit!))
        (let [result (q/exec session)]
          (when-let [e @error] (throw e))
          (when-not (zero? result)
            (throw (ex-info "Qt event loop failed." {:exit-code result})))))
      (finally
        ;; Stop admission before removing watchers; queued work is now inert.
        (reset! jobs {:session nil :work {}})
        (q/set-handler session ffi/null)
        (try
          (unmount!)
          (finally
            (abandon!)
            (q/close session)
            (when @callback (ffi/free-callable @callback))
            (w/clear!)
            (reset! b/loop-running? false)))))))

(def backend
  {:name :qt-quick
   :create! w/create!
   :dispose! w/dispose!
   :apply-props! w/apply-props!
   :append-child! w/append-child!
   :remove-child! w/remove-child!
   :replace-child! w/replace-child!
   :reorder-child! w/reorder-child!
   :schedule #(later! 0 %)
   :run-lifecycle (fn [opts lifecycle]
                    (host/call-on-main-thread #(run-session! opts lifecycle)))})

(defn install! [] (b/register! backend) nil)
(install!)
