(ns snitt.capture-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.native :as native]
            [snitt.sessions :as sessions]))

(def checks (atom 0))
(def completed? (atom false))

(defn check! [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {}))))

(defn step! [steps]
  (when-let [step (first steps)]
    (qt/later! 120 #(do (step) (step! (rest steps))))))

(defn services [manager id]
  (get-in (sessions/session manager id) [:services :root]))

(defn backend [manager id]
  (get-in (sessions/session manager id) [:services :backend]))

(defn invoke! [manager id signature args]
  (object/invoke! (services manager id) signature args))

(defn capture-id [manager]
  (:capture (sessions/snapshot manager)))

(defn exercise! [image fixture]
  (let [manager (:manager @app/runtime)
        first-id 1
        first-window (:window (sessions/session manager first-id))
        first-canvas (app/canvas (:context (sessions/session manager first-id)))
        second-id (sessions/open-image! manager (str "file://" image) "other.png")
        second-window (:window (sessions/session manager second-id))
        second-canvas (app/canvas (:context (sessions/session manager second-id)))
        current (atom nil)]
    (check! (and (q/read-property first-canvas "hasImage")
                 (q/read-property second-canvas "hasImage"))
            "Two independent source documents are open")
    (sessions/active! manager second-id)
    (qt/minimize! first-window)
    (let [first-visibility (q/read-property first-window "visibility")
          second-visibility (q/read-property second-window "visibility")]
      (step!
       [(fn []
          (reset! current (sessions/capture! manager false false))
          (check! (some? @current) "Capture admitted")
          (check! (nil? (sessions/capture! manager false false)) "Concurrent capture denied")
          (check! (not (q/read-property first-window "visible")) "First editor hidden")
          (check! (not (q/read-property second-window "visible")) "Second editor hidden")
          (invoke! manager @current "finishCaptureForTest(bool)" [false]))
        (fn []
          (check! (nil? (capture-id manager)) "Cancellation ends capture")
          (check! (= first-visibility (q/read-property first-window "visibility"))
                  "Minimized visibility restored")
          (check! (= second-visibility (q/read-property second-window "visibility"))
                  "Visible editor restored")
          (check! (= 2 (count (sessions/sessions manager)))
                  "Cancellation removes only the capture editor")
          (reset! current (sessions/capture! manager false false))
          (check! (invoke! manager @current "queueCaptureFileForTest(QString)" [fixture])
                  "Native adapter retains captured pixels")
          (spit fixture "temporary file already removed")
          (invoke! manager @current "finishCaptureForTest(bool)" [true]))
        (fn []
          (check! (nil? (capture-id manager)) "Single capture completes")
          (let [captured (sessions/session manager @current)
                captured-canvas (app/canvas (:context captured))]
            (check! (q/read-property captured-canvas "hasImage")
                    "Queued Jolt callback consumes retained image after file removal")
            (check! (= 800 (q/read-property captured-canvas "imageWidth"))
                    "Single capture preserves image pixels and dimensions")
            (check! (native/copy! second-canvas) "Source document can export pixels")
            (let [expected (q/clipboard-image @widget/session)
                  actual (do (check! (native/copy! captured-canvas)
                                     "Captured document can export pixels")
                             (q/clipboard-image @widget/session))]
              (try
                (check! (q/image-equal @widget/session expected actual)
                        "Captured pixels match the deterministic source exactly")
                (finally
                  (q/image-dispose @widget/session expected)
                  (q/image-dispose @widget/session actual))))
            (check! (q/read-property (:window captured) "visible")
                    "Completed capture activates its own editor")
            (check! (q/read-property first-canvas "hasImage") "First document survives capture")
            (check! (q/read-property second-canvas "hasImage") "Second document survives capture")
            (sessions/close! manager @current))
          (qt/hide! second-window)
          (reset! current (sessions/capture! manager true false))
          (check! (invoke! manager @current "queueCaptureRegionsForTest(QStringList)"
                          [[image image image]])
                  "Three native region images retained")
          (invoke! manager @current "finishCaptureForTest(bool)" [true]))
        (fn []
          (check! (nil? (capture-id manager)) "Multiple capture completes")
          (let [captured (sessions/session manager @current)
                captured-canvas (app/canvas (:context captured))]
            (check! (= 3 (q/read-property captured-canvas "regionCount"))
                    "Multiple regions enter the intended new document")
            (check! (= :arrange (get-in @(:state (:context captured)) [:arrangement :phase]))
                    "Jolt owns arrangement after multiple capture")
            (check! (not (q/read-property second-window "visible"))
                    "Previously hidden editor stays hidden")
            (sessions/close! manager @current))
          (reset! current (sessions/capture! manager false false))
          (check! (invoke! manager @current "queueCaptureFileForTest(QString)" [image])
                  "Abandoned operation has a pending image")
          (sessions/close! manager @current)
          (reset! current (sessions/capture! manager true false))
          (check! (invoke! manager @current "queueCaptureRegionsForTest(QStringList)"
                          [[image image]])
                  "Replacement operation receives its own image batch")
          (invoke! manager @current "finishCaptureForTest(bool)" [true]))
        (fn []
          (check! (nil? (capture-id manager)) "Replacement capture completes")
          (check! (= 2 (q/read-property (app/canvas
                                         (:context (sessions/session manager @current))) "regionCount"))
                  "Stale result cannot replace the newer capture")
          (check! (= 3 (count (sessions/sessions manager)))
                  "Stale completion cannot open another editor")
          (sessions/close! manager @current)
          (reset! current (sessions/capture! manager false false))
          (invoke! manager @current "failCaptureForTest(QString)"
                   ["Allow Snitt in System Settings → Privacy & Security → Screen & System Audio Recording, then quit and reopen Snitt."]))
        (fn []
          (check! (nil? (capture-id manager)) "Permission failure clears capture")
          (check! (= 2 (count (sessions/sessions manager))) "Permission failure restores editors")
          (check! (:error? @(:state (:context (sessions/session manager second-id))))
                  "Permission message reaches prior editor")
          (reset! current (sessions/capture! manager false false))
          (invoke! manager @current "failCaptureForTest(QString)"
                   ["Could not capture a screen. Check screen recording permission and that the desktop is unlocked."]))
        (fn []
          (check! (nil? (capture-id manager)) "Unavailable screen clears capture")
          (reset! current (sessions/capture! manager false false))
          (invoke! manager @current "failCaptureForTest(QString)"
                   ["Could not start the macOS screenshot tool."]))
        (fn []
          (check! (nil? (capture-id manager)) "Native failure clears capture")
          (check! (= 2 (count (sessions/sessions manager)))
                  "Failure leaves existing editors usable")
          (check! (q/read-property first-canvas "hasImage") "First document remains usable")
          (check! (q/read-property second-canvas "hasImage") "Second document remains usable")
          (doseq [id [first-id second-id]] (sessions/close! manager id))
          (reset! completed? true)
          (qt/quit!))]))))

(defn -main [& _]
  (let [image (System/getenv "SNITT_JOLT_IMAGE")
        fixture (System/getenv "SNITT_JOLT_CAPTURE_FILE")]
    (try
      (qt/run-application!
       #(do (app/start! :image image :capture-start (fn [_ _ _ _] nil))
            (qt/later! 250 #(exercise! image fixture)))
       :app-id "snitt-jolt-development" :keep-alive true :auto-quit-ms 8000)
      (check! @completed? "Capture sequence completed")
      (check! (zero? (widget/live-count)) "Managed widgets released")
      (check! (zero? (q/owned-count)) "Native objects released")
      (println "PASS:" @checks "Jolt capture-session checks")
      (finally (reset! app/runtime nil) (shutdown-agents)))))
