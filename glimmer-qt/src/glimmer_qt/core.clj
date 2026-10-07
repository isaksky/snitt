(ns glimmer-qt.core
  "Qt Quick backend with explicit root and session ownership. Requiring installs it.
  Use glimmer.core/run and reload!; each run owns one window and a native arena."
  (:require [clojure.string :as str]
            [glimmer.backend :as b]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]
            [jolt.ffi :as ffi]
            [jolt.host :as host]))

(defonce ^:private jobs (atom {:session nil :work {}}))
(defonce ^:private next-job (atom 0))

(defn- validate-delay! [delay-ms repeat?]
  (when-not (and (integer? delay-ms) (<= (if repeat? 1 0) delay-ms 2147483647))
    (throw (ex-info "Timer requires an int32 delay (positive for repeats)." {}))))

(defn owner!
  "Create a scheduling owner on the GUI thread. Destroy it with destroy-owner!.
  Existing widget handles can also own jobs; disposing their subtree cancels work."
  []
  (when-let [session (:session @jobs)] (q/owner session)))

(defn destroy-owner!
  "Destroy a scheduling owner on the GUI thread, canceling its pending work."
  [owner]
  (q/destroy-owner owner)
  nil)

(defn- schedule! [owner delay-ms repeat? work]
  (validate-delay! delay-ms repeat?)
  (when-not (ifn? work) (throw (ex-info "Timer requires a callable." {})))
  (when-not (or (nil? owner) (and (integer? owner) (pos? owner)))
    (throw (ex-info "Timer owner must be a positive handle or nil." {})))
  ;; Serialize admission with cancel, restart and managed delivery. Native calls
  ;; here only enqueue commands and never call managed code while this is held.
  (locking jobs
    (when-let [session (:session @jobs)]
      (let [token (swap! next-job inc)]
        (swap! jobs assoc-in [:work token]
               {:work work :repeat? repeat? :delay delay-ms :last 0.0 :generation 0})
        (if (zero? (q/schedule session (or owner 0) token delay-ms (if repeat? 1 0)))
          (do (swap! jobs update :work dissoc token) nil)
          token)))))

(defn later!
  "Run a thunk once on Qt's main thread. Returns a token, or nil after shutdown
  or owner disposal. The optional owner is a widget or owner! handle; otherwise
  work belongs to the session. Safe from workers. Zero delay always queues."
  ([delay-ms work] (later! nil delay-ms work))
  ([owner delay-ms work] (schedule! owner delay-ms false work)))

(defn every!
  "Repeat on Qt's main thread at a positive interval. Callback receives
  {:elapsed-seconds total :delta-seconds since-previous-tick}, measured by a
  monotonic clock from native timer installation/restart, independent of frames.
  Missed intervals are not replayed. Ownership and return value match later!."
  ([interval-ms work] (every! nil interval-ms work))
  ([owner interval-ms work] (schedule! owner interval-ms true work)))

(defn cancel!
  "Release a job and its closure. Safe from workers and inside its own callback.
  Idempotent; a callback already claimed for delivery may finish."
  [token]
  (locking jobs
    (when-let [session (:session @jobs)]
      (when (contains? (:work @jobs) token)
        (swap! jobs update :work dissoc token)
        (q/cancel session token))))
  nil)

(defn restart!
  "Restart an active job's delay and elapsed clock, optionally with a new delay.
  Returns true on success, false for canceled, completed or stale tokens. Safe
  from workers. A canceled job must be scheduled anew to avoid retaining closures."
  ([token] (restart! token nil))
  ([token delay-ms]
   (locking jobs
     (if-let [job (get-in @jobs [:work token])]
       (let [delay-ms (if (nil? delay-ms) (:delay job) delay-ms)]
         (validate-delay! delay-ms (:repeat? job))
         (if (= 1 (q/restart (:session @jobs) token delay-ms))
           (do
             (swap! jobs assoc-in [:work token]
                    (assoc job :delay delay-ms :last 0.0 :generation (inc (:generation job))))
             true)
           false))
       false))))

(defn pending-count [] (count (:work @jobs)))

(defn quit! []
  (when-let [session @w/session] (q/exit session 0))
  nil)

(defn- dispatch! [widget event value]
  (case event
    "shutdown" (locking jobs (reset! jobs {:session nil :work {}}))
    "work-canceled" (locking jobs (swap! jobs update :work dissoc widget))
    "work" (let [[generation elapsed] (str/split value #",")
                 generation (parse-long generation)
                 elapsed (parse-double elapsed)
                 job (locking jobs
                       (let [current (get-in @jobs [:work widget])
                             job (when (= generation (:generation current)) current)]
                         (when job
                           (if (:repeat? job)
                             (swap! jobs assoc-in [:work widget :last] elapsed)
                             (swap! jobs update :work dissoc widget)))
                         job))]
             (when job
               (if (:repeat? job)
                 ((:work job) {:elapsed-seconds elapsed :delta-seconds (- elapsed (:last job))})
                 ((:work job)))))
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
        (locking jobs (reset! jobs {:session nil :work {}}))
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
