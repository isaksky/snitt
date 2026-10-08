(ns snitt.recording-test
  (:require [clojure.test :refer [deftest is run-tests]]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.recording :as recording]
            [snitt.sessions :as sessions]))

(deftest transitions
  (let [s (recording/initial 7)
        protected (first (recording/transition s 7 :protected ""))
        ready (first (recording/transition protected 7 :ready ""))
        running (first (recording/transition ready 7 :recording ""))]
    (is (= [s []] (recording/transition s 6 :ready "")))
    (is (= [s []] (recording/transition s 7 :ready "")))
    (is (= [s []] (recording/transition s 7 :saved "unsafe.mp4")))
    (is (= :ready (:phase ready)))
    (is (= :recording (:phase running)))
    (doseq [phase [:preparing :ready :recording :finishing :saved :canceled :failed]]
      (let [view (recording/presentation (assoc s :phase phase))]
        (is (= ({:preparing "Starting…" :ready "Recording · 0:00" :recording "Recording · 0:00"
                 :finishing "Saving…" :saved "Saved" :canceled "Canceled" :failed "Failed"} phase)
               (:label view)))
        (is (= (contains? #{:ready :recording} phase) (:stop-enabled view)))
        (is (= (contains? #{:preparing :ready :recording} phase) (:cancel-enabled view)))
        (is (= (contains? recording/busy-phases phase) (:visible view)))))
    (let [waiting (first (recording/transition protected 7 :stop ""))
          [finishing effects] (recording/transition waiting 7 :ready "")]
      (is (= [:finish] effects))
      (is (= :finishing (:phase finishing)))
      (is (= [finishing []] (recording/transition finishing 7 :stop "")))
      (is (= [finishing []] (recording/transition finishing 7 :cancel ""))))
    (let [[canceling effects] (recording/transition s 7 :cancel "")]
      (is (= [:cancel] effects))
      (is (= [canceling []] (recording/transition canceling 7 :ready "")))
      (is (= :canceled (:phase (first (recording/transition canceling 7 :canceled "")))))))
  (doseq [terminal [:saved :failed :canceled]]
    (let [s (assoc (recording/initial 1) :started true :protected true)
          [done effects] (recording/transition s 1 terminal "result")]
      (is (= [:terminal] effects))
      (is (= [done []] (recording/transition done 1 terminal "duplicate")))
      (is (= [done []] (recording/transition done 1 :ready ""))))))

(def completed (atom false))
(defn check! [condition message] (when-not condition (throw (ex-info message {}))))
(defn later! [f] (qt/later! 120 f))
(defn source! [manager]
  (let [id (sessions/capture! manager false true)
        root (get-in (sessions/session manager id) [:services :root])]
    (check! (not (object/invoke! root "startRecorder(int)" [id])) "Cannot start without protection/source")
    (object/invoke! root "recordingSourceForTest()" []) id))
(defn owner [manager id] (:recording (sessions/session manager id)))
(defn signal! [manager id generation event data]
  (object/invoke! (get-in (sessions/session manager id) [:services :root])
                  "recorderEventForTest(int,QString,QString)" [generation event data]))

(defn success! [manager]
  (check! (nil? (:capture (sessions/snapshot manager))) "Capture restored")
  (check! (= 1 (count (sessions/sessions manager))) "Prior editor restored exactly once")
  (check! (q/read-property (:window (sessions/session manager 1)) "visible") "Prior editor visible")
  (sessions/close! manager 1)
  (reset! completed true)
  (qt/quit!))

(defn exercise! [manager]
  (let [id (source! manager)]
    (later!
     (fn []
       (if (= "protection-failure" (System/getenv "SNITT_JOLT_FAKE_RECORDER"))
         (do
           (check! (sessions/session manager id) "Protection failure retains its error editor")
           (check! (:error? @(:state (:context (sessions/session manager id)))) "Protection failure reported")
           (sessions/close! manager id)
           (success! manager))
         (let [o (owner manager id)
               root (:root o)
               controls (when o (:controls @(:windows o)))]
           (check! (some? o) (str "Missing owner: " @(:state (:context (sessions/session manager 1)))))
           (check! (= :preparing (:phase @(:state o))) "Preparing until native ready")
           (check! (= [1 0] (object/invoke! root "recorderCountsForTest()" [])) "Starts after protection")
           (check! (not (q/read-property (qt/find-by-id controls "recording-stop") "enabled")) "Stop disabled before ready")
           (check! (= 3 (count @(:windows o))) "All three overlays exist")
           (signal! manager id (dec id) "saved" "stale.mp4")
           (sessions/hotkey-capture! manager)
           (signal! manager id id "ready" "")
           (later!
            (fn []
              (check! (= :finishing (:phase @(:state o))) "Early hotkey stop waits for ready")
              (check! (= [1 1] (object/invoke! root "recorderCountsForTest()" [])) "Stops exactly once")
              (sessions/hotkey-capture! manager)
              (recording/dispatch! o :cancel)
              (check! (= [1 1] (object/invoke! root "recorderCountsForTest()" [])) "Finalizing ignores duplicate controls")
              (signal! manager id id "failed" "Fake finalization failure")
              (signal! manager id id "canceled" "")
              (later!
               (fn []
                 (check! (sessions/session manager id) "Failure retains its error editor")
                 (check! (widget/dialog-visible? (qt/find-by-id (:window (sessions/session manager id)) "capture-error-dialog"))
                         "Recording failure opens the capture error dialog")
                 (sessions/close! manager id)
                 (let [next-id (source! manager)]
                   (later!
                    (fn []
                      (let [next-owner (owner manager next-id)]
                        (recording/event! next-owner id :failed "stale")
                        (check! (= :preparing (:phase @(:state next-owner))) "Stale generation discarded")
                        (recording/dispatch! next-owner :cancel)
                        (later!
                         (fn []
                           (check! (= :canceled (:phase @(:state next-owner))) "Startup cancellation completes")
                           (check! @(:closed next-owner) "Owner teardown fences callbacks")
                           (recording/event! next-owner next-id :ready "late")
                           (success! manager)))))))))))))))))

(defn -main [& _]
  (let [{:keys [fail error]} (run-tests 'snitt.recording-test)]
    (when (pos? (+ fail error)) (System/exit 1)))
  (try
    (qt/run-application!
     #(do (app/start! :capture-start (fn [& _] nil))
          (qt/later! 200 #(exercise! (:manager @app/runtime))))
     :app-id "snitt-jolt-development" :keep-alive true :auto-quit-ms 7000)
    (check! @completed "Recording test completed")
    (check! (zero? (widget/live-count)) "Widgets released")
    (check! (zero? (q/owned-count)) "Native objects released")
    (println "PASS: Jolt recording controls, protection, cancellation and generations")
    (finally (reset! app/runtime nil) (shutdown-agents))))
