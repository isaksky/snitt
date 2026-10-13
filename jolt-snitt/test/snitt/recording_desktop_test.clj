(ns snitt.recording-desktop-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.recording :as recording]
            [snitt.sessions :as sessions]))

(def completed (atom false))
(defn check! [condition message] (when-not condition (throw (ex-info message {}))))
(defn wait-for! [label predicate action attempts]
  (qt/later! 50
    #(cond (predicate) (action)
           (zero? attempts) (throw (ex-info (str "Timed out: " label) {}))
           :else (wait-for! label predicate action (dec attempts)))))
(defn owner [manager id] (:recording (sessions/session manager id)))
(defn select! [manager id]
  (let [backend (get-in (sessions/session manager id) [:services :backend])]
    (check! (object/invoke! backend "pickerVideo(int,QRectF)" [0 (value/rect-f 80 120 320 200)])
            "Recording source selected")))

(defn cancel-test! [manager]
  (let [id (sessions/capture! manager false true)]
    (wait-for! "picker for cancellation" #(:picker (sessions/snapshot manager))
      #(do (select! manager id)
           (wait-for! "recording preparation" #(owner manager id)
             #(let [o (owner manager id)]
                (check! (= :preparing (:phase @(:state o))) "Real cancellation during preparation")
                (recording/dispatch! o :cancel)
                (wait-for! "native cancellation" #(nil? (sessions/session manager id))
                  #(do
                     (check! (= :canceled (:phase @(:state o))) "Native recorder canceled")
                     (check! (nil? (:capture (sessions/snapshot manager))) "Cancellation restored capture")
                     (check! (= 1 (count (sessions/sessions manager))) "Restored original editor")
                     (sessions/close! manager 1)
                     (reset! completed true)
                     (qt/quit!)) 200)) 100)) 300)))

(defn exercise! [manager]
  (let [id (sessions/capture! manager false true)]
    (wait-for! "real screen picker" #(:picker (sessions/snapshot manager))
      #(do
         (select! manager id)
         (wait-for! "native readiness"
           #(when-let [o (owner manager id)]
              (and (#{:ready :recording} (:phase @(:state o)))
                   (q/read-property (qt/find-by-id (:controls @(:windows o)) "recording-stop") "enabled")))
           #(let [o (owner manager id)
                  controls (:controls @(:windows o))]
              (check! (= [77 117 326 206]
                         (let [w (:outline @(:windows o))]
                           (mapv (fn [p] (q/read-property w p)) ["x" "y" "width" "height"])))
                      "Outline follows selected recording region")
              (check! (q/read-property (qt/find-by-id controls "recording-stop") "enabled") "Ready enables Stop")
              (qt/later! 650
                #(do
                   (sessions/hotkey-capture! manager)
                   (wait-for! "recording saved" #(= :saved (:phase @(:state o)))
                     #(do
                        (check! (nil? (:capture (sessions/snapshot manager))) "Save restores editors")
                        (check! (empty? @(:windows o)) "Saved recording closes overlays")
                        (check! (some? (:review-window (sessions/session manager id))) "Saved recording opens review")
                        (spit (System/getenv "SNITT_JOLT_RECORDING_PATH") (:path @(:state o)))
                        (sessions/close! manager id)
                        (cancel-test! manager)) 400)))) 400)) 300)))

(defn -main [& _]
  (try
    (qt/run-application!
     #(do (app/start!) (qt/later! 200 #(exercise! (:manager @app/runtime))))
     :app-id "snitt-jolt-development" :keep-alive true :auto-quit-ms 45000)
    (check! @completed "Real recording sequence completed")
    (check! (zero? (widget/live-count)) "Widgets released")
    (check! (zero? (q/owned-count)) "Native objects released")
    (println "PASS: real desktop recording, ready/stop/save/review and startup cancellation")
    (finally (reset! app/runtime nil) (shutdown-agents))))
