(ns snitt.desktop-test
  "Short macOS desktop check for the real Snitt tray and background lifetime."
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.tray :as tray]
            [snitt.app :as app]
            [snitt.sessions :as sessions]
            [snitt.settings :as settings]))

(defn check! [condition message]
  (when-not condition (throw (ex-info message {}))))

(defn -main [& _]
  (let [completed? (atom false)]
    (try
      (qt/run-application!
       #(do
          (app/start! :background true)
          (qt/later! 100
            (fn []
              (let [{:keys [manager tray show-action quit-action]} @app/runtime]
                (check! (and tray (q/alive? tray) (q/read-property tray "visible"))
                        "macOS desktop did not show the Snitt tray")
                (check! (empty? (sessions/sessions manager))
                        "Background launch must have no editor")
                (tray/dispatch! show-action "tray-triggered" "")
                (check! (some #(q/read-property (:window %) "visible")
                              (sessions/sessions manager))
                        "Tray show did not open an editor")
                (doseq [{:keys [id]} (sessions/sessions manager)]
                  (sessions/close! manager id))
                (check! (empty? (sessions/sessions manager))
                        "Last editor did not close independently")
                (check! (q/alive? tray) "Tray died with the last editor")
                (reset! completed? true)
                (tray/dispatch! quit-action "tray-triggered" "")))))
       :app-id "snitt-jolt-development" :keep-alive true :auto-quit-ms 5000)
      (check! @completed? "Desktop check did not complete")
      (println "PASS: macOS tray show/quit and background lifetime")
      (finally (settings/clear!) (reset! app/runtime nil) (shutdown-agents)))))
