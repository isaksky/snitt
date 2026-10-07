(ns hello.snitt-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [hello.snitt :as snitt]
            [jolt.ffi :as ffi]
            [jolt.host :as host]))

(ffi/defcfn write-images "hello_write_test_images" [:string] :int)
(def directory (System/getenv "SNITT_EXTENSION_FIXTURE_DIR"))
(def checks (atom 0))
(def notices (atom []))
(def done? (atom false))
(def handles (atom {}))

(defn check! [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {:notices @notices}))))
(defn property [handle name] (q/read-property handle name))
(defn canvas [] (widget/find-by-id "snitt-canvas"))
(defn service [] (widget/find-by-id "snitt-services"))

(defn root []
  [:overlay
   [:snitt/canvas
    {:id "snitt-canvas" :width 320 :height 240
     :qt/subscriptions
     {:image {:property "hasImage" :handler #(swap! notices conj [:image %])}
      :tool {:property "tool" :handler #(swap! notices conj [:tool %])}
      :geometry {:property "imageRect" :handler #(swap! notices conj [:geometry %])}}}]
   [:snitt/services {:id "snitt-services"}]])

(defn phase-two! []
  (let [{:keys [settings backend trim selector selector-child]} @handles]
    (check! (some #{[:image true]} @notices) "Canvas image notification reached Jolt")
    (check! (some #{[:tool "cut"]} @notices) "Canvas tool notification reached Jolt")
    (check! (some #(= :geometry (first %)) @notices) "Canvas geometry notification reached Jolt")
    (check! (= 240 (property (canvas) "imageWidth")) "Undo restored image width")
    (check! (not (property (canvas) "canUndo")) "Undo consumed the representative edit")
    (check! (some #(= :settings (first %)) @notices) "Settings notification reached Jolt")
    (check! (some #(= :settings-error (first %)) @notices) "Settings error reached Jolt")
    (check! (some #(= :capture-error (first %)) @notices) "Backend error reached Jolt")
    (check! (some #(= :capture-finished (first %)) @notices) "Fake capture completion reached Jolt")
    (check! (some #(= :trim-problem (first %)) @notices) "Trim problem notification reached Jolt")
    (check! (some #(= :trim-progress (first %)) @notices) "Trim progress notification reached Jolt")
    (check! (some #{[:recording-ready]} @notices) "Fake recording signal reached Jolt")
    (check! (some #{[:hotkey]} @notices) "Hotkey callback reached Jolt")
    (check! (some #{[:ipc "show"]} @notices) "Local IPC callback reached Jolt")
    (check! (= 0.0 (property trim "progress")) "Trim progress is readable")
    (check! (q/alive? selector-child) "QWidget selector is exposed as a borrowed child")
    (widget/dispose! selector)
    (check! (not (q/alive? selector-child)) "Disposing QWidget host invalidates selector")
    (check! (q/alive? settings) "AppSettings remains owned by service root")
    (check! (q/alive? backend) "Backend remains owned by service root")
    (reset! done? true)
    (qt/quit!)))

(defn phase-one! []
  (let [canvas (canvas)
        service (service)
        {:keys [settings backend trim]} (snitt/expose-services! service)
        selector (widget/create! :snitt/selector {:id "selector"})]
    (check! (every? q/alive? [canvas service settings backend trim selector])
            "Real Snitt objects were constructed in the shared session")
    (check! (property selector "sharedApplication") "QWidget host uses glimmer QApplication")
    (check! (property selector "iconsReady") "Selector sees Snitt icon resources")
    (check! (object/invoke! selector "expose()" []) "Selector child exposed")
    (let [selector-child (:value (property selector "selector"))]
      (swap! handles assoc :settings settings :backend backend :trim trim
             :selector selector :selector-child selector-child))
    (check! (= 1 (write-images directory)) "Generated PNG fixtures")
    (check! (snitt/load-image! canvas (str directory "/a # ü.png")) "Real EditorCanvas loaded PNG")
    (check! (= 240 (property canvas "imageWidth")) "Native image dimensions reached Jolt")
    (q/set-property! canvas "width" 300)
    (q/set-property! canvas "tool" "cut")
    (snitt/drag! canvas 80 80 180 80)
    (check! (property canvas "canUndo") "Native document edit entered undo history")
    (check! (< (property canvas "imageWidth") 240) "Native cut changed image geometry")
    (snitt/undo! canvas)

    (object/observe! service settings "goodColor" #(swap! notices conj [:settings %]))
    (object/subscribe! service settings "reloadFailed(QString)"
                       #(swap! notices conj [:settings-error %]))
    (object/subscribe! service backend "error(QString)"
                       #(swap! notices conj [:capture-error %]))
    (object/subscribe! service backend "captureFinished(bool)"
                       #(swap! notices conj [:capture-finished %]))
    (object/subscribe! service backend "recordingReady()"
                       #(swap! notices conj [:recording-ready]))
    (object/observe! service trim "problem" #(swap! notices conj [:trim-problem %]))
    (object/observe! service trim "progress" #(swap! notices conj [:trim-progress %]))
    (object/subscribe! service service "hotkeyActivated()"
                       #(swap! notices conj [:hotkey]))
    (object/subscribe! service service "ipcCommand(QString)"
                       #(swap! notices conj [:ipc %]))

    (check! (map? (property settings "shortcuts")) "Native shortcut map reached Jolt")
    (check! (vector? (get (property settings "shortcuts") "globalCapture"))
            "Global shortcut alternatives retain list shape")
    (spit (str directory "/settings.ini")
          "[Colors]\ngood=#123456\nbad=#abcdef\n[Shortcuts]\ncopyClose=Ctrl+Shift+C\n")
    (check! (snitt/reload-settings! service) "Valid isolated settings reload")
    (check! (= "#ff123456" (:value (property settings "goodColor"))) "Color reaches Jolt")
    (check! (some #{"Meta+Shift+C" "Ctrl+Shift+C"}
                  (get (property settings "shortcuts") "copyClose"))
            "Configured shortcut reaches Jolt in Qt portable form")
    (spit (str directory "/settings.ini") "[Colors]\ngood=broken\n")
    (check! (not (snitt/reload-settings! service)) "Bad settings rejected")
    (check! (= "#ff123456" (:value (property settings "goodColor")))
            "Invalid reload retained previous color")
    (object/invoke! backend "capture(bool,bool)" [false false])
    (object/invoke! backend "fakeRecording()" [])
    (spit (str directory "/dummy.mp4") "fixture")
    (snitt/open-trim! service (str directory "/dummy.mp4"))
    (object/invoke! trim "exportRange(qlonglong,qlonglong)" [0 50])
    (check! (re-find #"at least" (property trim "problem")) "Real TrimSession reports invalid range")
    (object/invoke! service "fakeHotkey()" [])
    (check! (object/invoke! service "sendIpc(QString)" ["show"]) "Local IPC request sent")
    (qt/later! 200 phase-two!)))

(defn -main [& _]
  (try
    (check! (string? directory) "Fixture directory was supplied")
    (qt/run-application!
     #(do (snitt/install! directory)
          (qt/open-window! root :width 320 :height 240)
          (qt/later! 100 phase-one!))
     :auto-quit-ms 5000)
    (check! @done? "Snitt extension test completed")
    (check! (zero? (widget/live-count)) "Managed Snitt handles cleaned up")
    (host/call-on-main-thread
     #(check! (zero? (q/owned-count)) "Native Snitt objects cleaned up"))
    (println "PASS:" @checks "Snitt extension checks")
    (finally (shutdown-agents))))
