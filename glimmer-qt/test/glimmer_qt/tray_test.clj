(ns glimmer-qt.tray-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.tray :as tray]
            [glimmer-qt.lifecycle-test :refer [check! throws! clean!]]
            [jolt.ffi :as ffi]))

(ffi/defcfn desktop-menu "gqt_test_tray_desktop" [:uint64 :int] :int :blocking)
(ffi/defcfn tray-events "gqt_test_tray_events" [:uint64] :int)

(defn icon-path []
  (str (System/getProperty "user.dir") "/../src/icons/scissors.svg"))

(defn -main [& _]
  (try
    (let [desktop? (= "1" (System/getenv "SNITT_TRAY_DESKTOP_CHECK"))
          calls (atom []) tray-id (atom nil) capture (atom nil)
          editor (atom nil) quit (atom nil) window (atom nil)
          finished (atom false)]
      (qt/run-application!
       (fn []
         (reset! tray-id (tray/create! (icon-path) :tooltip "Snitt tray test"
                                       :on-activate #(swap! calls conj [:activate %])
                                       :on-message-click #(swap! calls conj :message)))
         (reset! window (qt/open-window! (fn [] [:label {:label "Editor"}]) :visible false))
         (reset! capture (tray/add-action! @tray-id :label "Capture region"
                                          :shortcut-hint "⌘⇧5"
                                          :on-trigger #(swap! calls conj :old)))
         (reset! editor (tray/add-action! @tray-id :label "Open editor"
                                         :on-trigger #(do (swap! calls conj :editor)
                                                         (qt/show! @window))))
         (tray/add-separator! @tray-id)
         (reset! quit (tray/add-action! @tray-id :label "Quit Snitt"
                                       :on-trigger #(do (swap! calls conj :quit)
                                                       (qt/quit!))))
         (let [temporary (tray/create! (icon-path))
               temp-action (tray/add-action! temporary :label "Temporary"
                                            :on-trigger #(swap! calls conj :stale))]
           (tray/destroy! temporary)
           (check! (and (not (q/alive? temporary)) (not (q/alive? temp-action)))
                   "Explicit tray disposal invalidates icon and action")
           (throws! #(tray/update-action! temp-action :label "Stale") #"disposed"))
         (check! (= 1 (qt/window-count)) "Hidden editor remains owned")
         (check! (false? (q/read-property @window "visible")) "Editor starts hidden")
         (tray/set-tooltip! @tray-id "Capture is ready")
         (check! (= "Capture is ready" (q/read-property @tray-id "toolTip"))
                 "Tray tooltip updates in place")
         (check! (= "Capture region\t⌘⇧5" (q/read-property @capture "text"))
                 "Native QAction displays shortcut hint")
         (when desktop?
           (check! (tray/available?) "macOS desktop exposes a system tray"))
         (when-not (tray/available?)
           (throws! #(tray/show! @tray-id) #"System tray is unavailable")
           (throws! #(tray/notify! @tray-id "Title" "Body") #"System tray is unavailable"))
         (when (tray/available?)
           (tray/show! @tray-id))
         (let [editor-handle @editor]
           (tray/update-action! @capture :shortcut-hint "⌘⇧6"
                                :on-trigger #(swap! calls conj :new))
           (check! (and (= editor-handle @editor)
                        (q/alive? editor-handle)
                        (= "Open editor" (q/read-property editor-handle "text")))
                   "Unrelated native action remains intact"))
         (check! (= "Capture region\t⌘⇧6" (q/read-property @capture "text"))
                 "Shortcut hint updates in place")
         (q/invoke! @capture "trigger()" [])
         (check! (= [:new] @calls) "Replacement callback fires once")
         (check! (= 1 (tray-events @tray-id)) "Native activation signals delivered")
         (check! (= [:new [:activate :double-click] :message] @calls)
                 "Activation reason and notification click delivered")
         (tray/set-handlers! @tray-id {:on-activate #(swap! calls conj [:new-activation %])
                                      :on-message-click nil})
         (check! (= 1 (tray-events @tray-id)) "Native activation signals remain connected")
         (check! (= [:new [:activate :double-click] :message [:new-activation :double-click]] @calls)
                 "Activation callback replacement has no duplicate delivery")
         (throws! #(tray/notify! @tray-id "Title" "Body" :kind :unsupported)
                  #"Invalid Qt tray notification")
         (tray/update-action! @capture :enabled false)
         (check! (false? (q/read-property @capture "enabled")) "Native action disabled")
         (tray/update-action! @capture :enabled true :checkable true :checked true)
         (check! (true? (q/read-property @capture "checked")) "Native checked action")
         (q/invoke! @capture "trigger()" [])
         (check! (false? (q/read-property @capture "checked")) "Native toggle changes checked state")
         (tray/update-action! @capture :shortcut-hint "⌘⇧7")
         (check! (false? (q/read-property @capture "checked"))
                 "Text change preserves native checked state")
         (q/invoke! @editor "trigger()" [])
         (check! (= [:new [:activate :double-click] :message
                     [:new-activation :double-click] :new :editor] @calls)
                 "Editor action dispatches")
         (qt/later! 60
                    #(do
                       (when desktop?
                         (let [result (desktop-menu @tray-id 4)]
                           (check! (= 1 result)
                                   (str "Actual macOS tray icon and native menu opened: "
                                        (when (zero? result) (q/native-error))))))
                       (check! (true? (q/read-property @window "visible")) "Editor action shows window")
                       (qt/hide! @window)
                       (check! (q/alive? @tray-id) "Tray survives hidden editor")
                       (q/invoke! @capture "trigger()" [])
                       (check! (= [:new [:activate :double-click] :message
                                   [:new-activation :double-click] :new :editor :new] @calls)
                               "No duplicate trigger")
                       (q/invoke! @quit "trigger()" [])
                       (reset! finished true))))
       :auto-quit-ms 4000)
      (check! @finished "Explicit quit action exited application")
      (check! (= :quit (last @calls)) "Quit callback ran")
      (clean! [])
      (check! (not (q/alive? @tray-id)) "Tray handle invalidated on shutdown")
      (println "PASS:" @glimmer-qt.lifecycle-test/assertions "tray checks"))
    (finally (shutdown-agents))))
