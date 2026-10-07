(ns glimmer-qt.core
  "Qt Quick backend with independent windows in one application runtime.
  glimmer.core/run remains the single-root convenience entry point."
  (:require [clojure.string :as str]
            [glimmer.backend :as b]
            [glimmer.core :as ui]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]
            [glimmer-qt.object :as object]
            [jolt.ffi :as ffi]
            [jolt.host :as host]))

(defonce ^:private jobs (atom {:session nil :work {}}))
(defonce ^:private next-job (atom 0))
(defonce ^:private windows (atom {}))
(defonce ^:private keep-alive? (atom false))

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

(defn- gui-thread! []
  (when-not (= 1 (q/gui-thread))
    (throw (ex-info "Qt window operation requires the main thread." {}))))

(defn- window! [window]
  (gui-thread!)
  (or (get @windows window)
      (throw (ex-info "Invalid or disposed Qt window." {:window window}))))

(defn window-container
  "Return this window's mount point; valid only while the window is alive."
  [window]
  (:container (window! window)))

(defn find-by-id [window id] (w/find-by-id (window-container window) id))
(defn window-alive? [window] (contains? @windows window))
(defn window-count [] (count @windows))

(defn- action! [window action]
  (window! window)
  (q/window-action window action)
  nil)
(defn show! [window] (action! window 0))
(defn hide! [window] (action! window 1))
(defn minimize! [window] (action! window 2))
(defn restore! [window] (action! window 3))
(defn activate! "Show, restore if minimized, raise and request activation." [window] (action! window 4))
(defn raise! [window] (action! window 5))
(defn request-activate! [window] (action! window 6))
(defn request-close! "Queue the same policy callback as a native window close." [window] (action! window 7))

(defn- dispose-window! [window]
  (when-let [{:keys [container unmount! abandon!]} (get @windows window)]
    (swap! windows dissoc window)
    (try
      (unmount!)
      (finally
        (abandon!)
        (try (w/forget-window! container)
             (finally (when (q/alive? window) (q/window-destroy window))))))))

(defn close-window!
  "Accept closure without consulting policy again. Disposal is queued, so this
  is safe inside a control/close callback. Repeated or stale closes are harmless."
  [window]
  (gui-thread!)
  (when-let [data (get @windows window)]
    (when-not (:closing? data)
      (swap! windows assoc-in [window :closing?] true)
      (later! window 0
              #(do (dispose-window! window)
                   (when (and (empty? @windows) (not @keep-alive?)) (quit!))))))
  nil)

(defn- create-window! [opts lifecycle]
  (when-not @w/session (throw (ex-info "No running Qt application." {})))
  (let [{:keys [title width height on-close visible]
         :or {title "Glimmer · Qt" width 560 height 360 visible true}} opts]
    (when-not (and (string? title) (integer? width) (<= 1 width 2147483647)
                   (integer? height) (<= 1 height 2147483647) (boolean? visible)
                   (or (nil? on-close) (ifn? on-close)))
      (throw (ex-info "Invalid Qt window options." {})))
    (let [window (q/window-create @w/session title width height)
          container (w/register-window! (q/window-container window))]
      (swap! windows assoc window (assoc lifecycle :container container :on-close on-close))
      (try
        ((:mount! lifecycle) container :window)
        (when visible (show! window))
        window
        (catch Throwable e (dispose-window! window) (throw e))))))

(defn open-window!
  "Mount an independent root on the GUI thread. Options: :title, :width, :height,
  :visible (default true), :on-close (fn [window]). A callback must return true
  to accept; false/nil defers. Without a callback a request closes this window.
  Use close-window! later to accept a deferred request. Returns an opaque handle."
  [root-fn & {:as opts}]
  (let [inst (atom nil) root (atom root-fn) mount-point (atom nil)
        unmount! (fn [] (when-let [current @inst]
                         (reset! inst nil)
                         (ui/unmount! current @mount-point :window)))
        mount! (fn [container _]
                 (reset! mount-point container)
                 (reset! inst (ui/mount container :window [@root])))
        abandon! (fn [] (when-let [current @inst]
                         (reset! inst nil) (ui/abandon! current)))]
    (create-window! opts {:mount! mount! :unmount! unmount! :abandon! abandon!
                          :reload! (fn [new-root]
                                     (unmount!)
                                     (when new-root (reset! root new-root))
                                     (mount! @mount-point :window))})))

(defn reload-window!
  "Queue a remount of only this independent root; closing cancels queued reloads."
  ([window] (reload-window! window nil))
  ([window root-fn]
   (let [reload! (:reload! (window! window))]
     (when-not reload! (throw (ex-info "Use glimmer.core/reload! for the run root." {})))
     (later! window 0 #(when-let [data (get @windows window)]
                        (when-not (:closing? data) (reload! root-fn)))))
   nil))

(defn- abandon-windows! []
  (doseq [[_ data] @windows] ((:abandon! data))))

(defn- dispatch! [widget event value]
  (case event
    "close-request" (when-let [{:keys [on-close closing?]} (get @windows widget)]
                      (when-not closing?
                        (when (or (nil? on-close) (true? (on-close widget)))
                          (close-window! widget))))
    ("signal" "signal-error" "subscription-ended") (object/dispatch! widget event value)
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

(defn- run-application* [opts start!]
  (let [{:keys [app-id on-ready auto-quit-ms keep-alive]
         :or {app-id "glimmer.qt" keep-alive true}} opts
        session (q/open-application app-id)
        error (atom nil)
        callback (atom nil)]
    (try
      (reset! w/session session)
      (reset! keep-alive? keep-alive)
      (reset! jobs {:session session :work {}})
      (reset! b/loop-running? true)
      (reset! callback
              (ffi/foreign-callable
               (fn [widget event value]
                 (when-not @error
                   (try (dispatch! widget event value)
                        (catch Throwable e
                          ;; Never unwind managed exceptions through C++.
                          (reset! error e)
                          (try (abandon-windows!) (catch Throwable _ nil))
                          (q/exit session 1)))))
               [:uint64 :string :string] :void :collect-safe))
      (q/set-handler session @callback)
      (start!)
      (when on-ready (later! 0 on-ready))
      (when auto-quit-ms (later! auto-quit-ms quit!))
      (let [result (q/exec session)]
        (when-let [e @error] (throw e))
        (when-not (zero? result)
          (throw (ex-info "Qt event loop failed." {:exit-code result}))))
      (finally
        (locking jobs (reset! jobs {:session nil :work {}}))
        (q/set-handler session ffi/null)
        (let [cleanup-error (atom nil)]
          (doseq [window (keys @windows)]
            (try (dispose-window! window) (catch Throwable e (reset! cleanup-error e))))
          (try
            (q/close session)
            (finally
              (when @callback (ffi/free-callable @callback))
              (try (w/clear!)
                   (finally (object/clear!) (reset! windows {}) (reset! b/loop-running? false)))))
          (when (and @cleanup-error (not @error)) (throw @cleanup-error)))))))

(defn run-application!
  "Run one QApplication runtime on the process main thread. start! may create
  zero or more windows and retained native services. Stays alive with no windows
  by default; :keep-alive false quits after the final accepted window close.
  quit! tears down every root and native object before returning. GUI operations
  belong in start!, callbacks or later!; they reject worker-thread access."
  [start! & {:as opts}]
  (host/call-on-main-thread #(run-application* opts start!)))

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
                    (host/call-on-main-thread
                     #(run-application* (merge {:keep-alive false} opts)
                                        (fn [] (create-window! opts lifecycle)))))})

(defn install! [] (b/register! backend) nil)
(install!)
