(ns glimmer-qt.tray-desktop
  "Short-lived macOS menu-bar check. Run with QT_QPA_PLATFORM=cocoa
  jolt -A:tray-test -m glimmer-qt.tray-desktop and open the Snitt icon."
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.tray :as tray]))

(defn -main [& _]
  (try
    (qt/run-application!
     (fn []
       (let [icon (str (System/getProperty "user.dir") "/../src/icons/scissors.svg")
             tray-id (tray/create! icon :tooltip "Snitt tray desktop check")
             editor (atom nil)]
         (tray/add-action! tray-id :label "Capture region" :shortcut-hint "⌘⇧5"
                           :on-trigger #(println "Capture requested"))
         (tray/add-action! tray-id :label "Open editor"
                           :on-trigger #(if @editor
                                          (qt/activate! @editor)
                                          (reset! editor (qt/open-window! (fn [] [:label {:label "Editor shown from tray"}])
                                                                         :title "Snitt tray check"))))
         (tray/add-separator! tray-id)
         (tray/add-action! tray-id :label "Quit Snitt" :on-trigger qt/quit!)
         (tray/show! tray-id)
         (println "TRAY READY: open the Snitt menu-bar icon; demo exits after 30 seconds.")))
     :auto-quit-ms 30000)
    (finally (shutdown-agents))))
