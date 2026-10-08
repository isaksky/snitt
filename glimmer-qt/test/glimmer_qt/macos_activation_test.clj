(ns glimmer-qt.macos-activation-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [jolt.ffi :as ffi]))

(ffi/defcfn background! "gqt_test_macos_background" [] :int)
(ffi/defcfn application-active? "gqt_test_macos_application_active" [] :int)
(ffi/defcfn native-visible? "gqt_test_macos_window_visible" [:pointer] :int)

(defn check! [condition message]
  (when-not condition (throw (ex-info message {}))))

(defn window-visible? [window]
  (let [visible? (atom false)]
    (qt/with-native-window! window
      (fn [pointer] (reset! visible? (= 1 (native-visible? pointer))) true))
    @visible?))

(defn exercise! [actions finished]
  (if-let [[label activate!] (first actions)]
    (do
      (println "Testing inactive accessory app:" label)
      (background!)
      (qt/later! 200
        (fn []
          (check! (zero? (application-active?)) "Accessory app starts inactive")
          (let [window (qt/open-window! (fn [] [:label {:label "Background capture activation test"}])
                                       :title "Snitt activation regression"
                                       :width 360 :height 120 :tool true :frameless true :topmost true)]
            (activate! window)
            (qt/later! 250
              (fn []
                (check! (= 1 (application-active?)) "Activation brings the accessory app forward")
                (check! (window-visible? window) "Activation makes the native tool panel visible")
                (check! (q/read-property window "active") "Tool panel receives keyboard focus")
                (qt/close-window! window)
                (exercise! (rest actions) finished)))))))
    (do (reset! finished true) (qt/quit!))))

(defn -main [& _]
  (let [finished (atom false)]
    (try
      (qt/run-application!
        #(qt/later! 250 (fn [] (exercise! [[:activate qt/activate!]
                                           [:request-activate qt/request-activate!]] finished)))
        :app-id "snitt-activation-test" :keep-alive true :auto-quit-ms 5000)
      (check! @finished "Activation regression sequence completed")
      (println "PASS: macOS accessory app activation, native tool visibility and keyboard focus")
      (finally (shutdown-agents)))))
