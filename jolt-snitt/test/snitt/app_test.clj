(ns snitt.app-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.native :as native]))

(def checks (atom 0))
(def completed? (atom false))

(defn check! [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {}))))

(defn exercise! [image]
  (let [{:keys [window services]} @app/runtime
        canvas (app/canvas)
        {:keys [root settings backend trim]} services
        directory (System/getenv "SNITT_JOLT_DATA_DIR")]
    (check! (qt/window-alive? window) "Jolt owns a live editor window")
    (check! (every? q/alive? [canvas root settings backend trim])
            "Real Snitt canvas and services share the Qt session")
    (check! (.startsWith (q/read-property settings "settingsFile") directory)
            (str "Development settings stay under the isolated data directory: "
                 (q/read-property settings "settingsFile") " / " directory))
    (check! (native/load-image! canvas image) "Native canvas loads an image")
    (check! (q/read-property canvas "hasImage") "Image is visible to Jolt")
    (native/begin! canvas 100 100)
    (native/end! canvas 200 200)
    (check! (q/read-property canvas "canUndo") "Rectangle enters native history")
    (native/undo! canvas)
    (check! (q/read-property canvas "canRedo") "Undo retains redo")
    (native/redo! canvas)
    (check! (q/read-property canvas "canUndo") "Redo restores edit")
    (check! (native/copy! canvas) "Edited image copies through native clipboard")
    (let [saved (native/save! canvas)]
      (check! (and (string? saved) (.startsWith saved directory))
              "Save writes to the isolated pictures directory"))
    (widget/dispose! root)
    (check! (not (q/alive? settings)) "Service disposal invalidates settings")
    (check! (not (q/alive? backend)) "Service disposal invalidates backend")
    (check! (not (q/alive? trim)) "Service disposal invalidates trim")
    (reset! completed? true)
    (qt/close-window! window)))

(defn -main [& _]
  (let [image (str (System/getProperty "user.dir")
                   "/../experiment/evidence/acceptance-macos.png")]
    (try
      (qt/run-application!
       #(do (app/start!) (qt/later! 250 #(exercise! image)))
       :app-id "snitt-jolt-development" :keep-alive false :auto-quit-ms 5000)
      (check! @completed? "Editor exercised before event loop exit")
      (check! (zero? (widget/live-count)) "Managed widgets released")
      (check! (zero? (q/owned-count)) "Native objects released")
      (println "PASS:" @checks "Snitt Jolt application checks")
      (finally (reset! app/runtime nil) (shutdown-agents)))))
