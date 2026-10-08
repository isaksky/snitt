(ns snitt.ui-parity-test
  "Matched window renders; complements desktop and packaged application inspection."
  (:require [clojure.string :as str]
            [jolt.ffi :as ffi]
            [clojure.java.io :as io]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as widget]
            [glimmer-qt.object :as object]
            [snitt.app :as app]
            [snitt.review :as review]
            [snitt.recording :as recording]
            [snitt.region :as region]
            [glimmer.ratom :as r]
            [snitt.native :as native]
            [snitt.sessions :as sessions]
            [snitt.settings :as settings]))

(ffi/defcfn pointer-native "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(ffi/defcfn tab-focus-native "gqt_test_tab_focus" [:uint64] :int :blocking)
(defn pointer! [id action x y] (pointer-native (widget/find-by-id id) action (double x) (double y)))

(def ids ["editor-header" "annotation-toolbar" "annotation-tool-cluster" "annotation-tools"
          "tool-cut" "rect" "tool-highlight" "tool-text" "arrow" "tool-blur" "tool-erase"
          "shortcut-cut" "shortcut-rect" "shortcut-highlight" "shortcut-text" "shortcut-arrow"
          "shortcut-blur" "shortcut-erase" "undo" "redo" "mode-group" "mode-segmented-control"
          "ink-good" "ink-bad" "mode-heading" "annotation-hint" "finish-group" "finish-buttons"
          "save" "copy" "finish-heading" "snitt-canvas" "editor-footer" "preview-status"
          "annotation-text-frame" "annotation-text" "annotation-text-resize-handle"
          "empty-heading" "empty-hint" "capture" "open" "paste" "capture-multiple" "record"])

(defn geometry []
  (into {}
        (keep (fn [id]
                (when-let [handle (widget/find-by-id id)]
                  (let [[x y] (q/map-point handle (q/window-container (:window @app/runtime)) 0 0)]
                    [id (assoc (into {} (keep (fn [property]
                                               (try [property (q/read-property handle property)]
                                                    (catch Throwable _ nil)))
                                             ["width" "height" "visible" "text" "hovered" "hoverEnabled" "pressed" "visualFocus" "font.pixelSize" "font.family" "font.weight"
                                              "contentHeight" "topPadding" "leftPadding" "bottomPadding" "rightPadding"
                                              "topInset" "bottomInset" "contentY" "cursorPosition"]))
                               "x" x "y" y)]))) ids)))

(defn selector-snapshots! [output done!]
  (let [screen (r/atom {"index" 0 "width" 1100 "height" 760 "pixelWidth" 800 "pixelHeight" 800
                        "source" (str "file://" (System/getenv "SNITT_UI_IMAGE"))})
        picker {:state (r/atom (region/initial false false))}
        window (qt/open-window! #(region/view picker @screen (fn [& _])) :width 1100 :height 760)]
    (letfn [(step! [states]
              (if-let [[name width height changes] (first states)]
                (do (q/set-property! window "width" width) (q/set-property! window "height" height)
                    (swap! screen assoc "width" width "height" height)
                    (swap! (:state picker) merge changes)
                    (qt/later! 250 #(do (pointer-native (qt/find-by-id window "picker-root-0") 1 (- width 5.0) (- height 5.0))
                                      (q/window-snapshot window (str output "/" name ".png"))
                                      (let [toolbar (qt/find-by-id window "picker-toolbar-0")]
                                        (spit (str output "/" name ".edn")
                                          (pr-str (into {} (for [alias ["singleButton" "multipleButton" "videoButton" "arrangeButton" "cancelButton"]
                                                               :let [b (q/child-target toolbar alias)]]
                                                            [alias (into {} (for [prop ["x" "y" "width" "height" "font.family" "font.pixelSize" "font.weight" "text" "fullWidth"]]
                                                                              [prop (q/read-property b prop)]))])))))
                                      (let [toolbar (qt/find-by-id window "picker-toolbar-0")]
                                        (spit (str output "/" name ".tsv")
                                          (str/join "\n"
                                            (for [alias ["singleButton" "multipleButton" "videoButton" "arrangeButton" "cancelButton"]
                                                  :let [b (q/child-target toolbar alias)]
                                                  prop ["x" "y" "width" "height" "visible"]]
                                              (str alias "\t" prop "\t" (q/read-property b prop))))))
                                      (step! (rest states)))))
                (do (qt/close-window! window) (done!))))]
      (step! [["selector-single" 1100 760 {}]
              ["selector-multiple" 1100 760 {:mode :multiple :regions [{:screen 0 :token 0 :area [100 180 200 120]}
                                                                      {:screen 0 :token 1 :area [450 380 20 20]}]}]
              ["selector-small" 440 500 {}]
              ["selector-video" 1100 760 {:mode :video :regions []}]
              ["selector-drag" 1100 760 {:mode :single :notice "Selection could not be captured. Try again."
                                          :drag {:screen 0 :start [100 180] :end [500 740]}}]
              ["selector-fractional" 1100 760 {:mode :multiple :notice "" :drag nil
                  :regions [{:screen 0 :token 0 :area [100.5 180.5 200.5 120.5]}
                            {:screen 0 :token 1 :area [450.5 380.5 20.5 20.5]}]}]]))))

(defn recording-snapshots! [output done!]
  (let [owner {:state (r/atom (recording/initial 1))}
        window (qt/open-window! #(recording/controls owner) :width 340 :height 58 :transparent-background true)]
    (letfn [(step! [states]
              (if-let [[name phase elapsed] (first states)]
                (do (swap! (:state owner) assoc :phase phase :elapsed elapsed)
                    (qt/later! 250 #(do (q/window-snapshot window (str output "/" name ".png")) (step! (rest states)))))
                (do (qt/close-window! window)
                    (let [indicator (qt/open-window! (fn [] [:recording-indicator {:hexpand true :vexpand true :completing true}])
                                                     :width 180 :height 180 :transparent-background true)]
                      (qt/later! 250 #(do (q/window-snapshot indicator (str output "/recording-ready.png"))
                                          (qt/close-window! indicator) (selector-snapshots! output done!)))))))]
      (step! [["recording-starting" :preparing 0] ["recording-running" :recording 65]
              ["recording-finishing" :finishing 65]]))))

(defn media-snapshots! [output done!]
  (let [owner {:state (r/atom (review/initial 0)) :window (atom nil)}
        window (qt/open-window! #(review/view owner) :title "Snitt — Review recording" :width 960 :height 690)]
    (reset! (:window owner) window)
    (letfn [(step! [states]
              (if-let [[name width height changes] (first states)]
                (do (q/set-property! window "width" width)
                    (q/set-property! window "height" height)
                    (swap! (:state owner) merge changes)
                    (qt/later! 250
                      #(do (q/window-snapshot window (str output "/" name ".png"))
                           (spit (str output "/" name ".edn")
                             (pr-str (into {} (for [id ["review-root" "review-preview" "review-video" "review-playback-controls" "review-trim" "review-save"]
                                                  :let [h (qt/find-by-id window id)]]
                                               [id {:point (q/map-point h (q/window-container window) 0 0)
                                                    :width (q/read-property h "width") :height (q/read-property h "height")
                                                    :implicitHeight (q/read-property h "implicitHeight")}]))))
                           (step! (rest states)))))
                (do (qt/close-window! window) (recording-snapshots! output done!))))]
      (step! [["review-empty" 960 690 {}]
              ["review-loaded" 960 690 {:duration 12.0 :end 12.0}]
              ["review-trimmed" 960 690 {:start 2.0 :end 10.0 :playhead 6.0}]
              ["review-small" 680 520 {}]
              ["review-exporting" 960 690 {:busy true :progress 0.35}]
              ["review-error" 960 690 {:busy false :problem "Could not save the trimmed recording."}]]))))

(defn -main [& _]
  (let [done? (atom false) output (System/getenv "SNITT_UI_OUTPUT")
        image (System/getenv "SNITT_UI_IMAGE")]
    (.mkdirs (io/file output))
    (try
      (qt/run-application!
       #(do
          (app/start!)
          (let [window (:window @app/runtime)
                snapshot! (fn [name]
                            (when-not (q/save-window @widget/session (str output "/" name ".png"))
                              (throw (ex-info "Snapshot failed" {:name name})))
                            (spit (str output "/" name ".tsv")
                                  (str/join "\n" (for [[id properties] (geometry)
                                                       [field value] properties]
                                                   (str/join "\t" [id field (pr-str value)])))))
                steps [["empty-wide" 1100 760 nil]
                       ["empty-small" 860 560 nil]
                       ["populated-wide" 1100 760 (fn [] (native/load-image! (app/canvas) image))]
                       ["populated-small" 860 560 nil]
                       ["populated-large" 1400 900 nil]
                       ["tool-hover" 1100 760 (fn [] (pointer! "rect" 1 20 20))]
                       ["tool-pressed" 1100 760 (fn [] (pointer! "rect" 0 20 20))]
                       ["copy-hover" 1100 760 (fn [] (pointer! "snitt-canvas" 2 0 0) (pointer! "copy" 1 20 20))]
                       ["copy-pressed" 1100 760 (fn [] (pointer! "copy" 0 20 20))]
                       ["ink-focus" 1100 760 (fn [] (pointer! "snitt-canvas" 2 0 0)
                         (tab-focus-native (widget/find-by-id "ink-good")))]
                       ["text-draft" 1100 760
                        (fn []
                          (app/dispatch! (:context @app/runtime) "toolText")
                          (#'app/start-text! (:context @app/runtime) 200.0 180.0)
                          (swap! app/state assoc-in [:draft :text] "Entry baseline\nSecond line"))]
                       ["text-committed" 1100 760
                        (fn [] (#'app/commit-text! (:context @app/runtime)))]
                       ["arrangement-wide" 1100 760
                        (fn [] (object/invoke! (get-in @app/runtime [:services :root]) "loadRegionFilesForTest(QStringList)"
                                               [(mapv #(str output "/../region-" % ".png") (range 3))]))]
                       ["arrangement-small" 860 560 nil]
                       ["arrangement-annotated" 1100 760 (fn [] (app/dispatch! (:context @app/runtime) "arrange"))]
                       ["dialog-rearrange" 1100 760
                        (fn [] (widget/open-dialog! (widget/find-by-id "rearrange-dialog")))]
                       ["dialog-reveal" 1100 760
                        (fn [] (widget/reject-dialog! (widget/find-by-id "rearrange-dialog"))
                          (swap! app/state assoc :saved-path "/tmp/sn itt.png")
                          (qt/later! 250 #(widget/open-dialog! (widget/find-by-id "reveal-dialog"))))]
                       ["dialog-capture-permission" 1100 760
                        (fn [] (widget/reject-dialog! (widget/find-by-id "reveal-dialog"))
                          (object/invoke! (app/canvas) "clear()" [])
                          (qt/later! 250 #(#'app/show-capture-error! (:context @app/runtime)
                            "Allow Snitt in System Settings → Privacy & Security → Screen & System Audio Recording, then quit and reopen Snitt.")))]
                       ["dialog-capture-error" 1100 760
                        (fn [] (widget/reject-dialog! (widget/find-by-id "capture-error-dialog"))
                          (qt/later! 250 #(#'app/show-capture-error! (:context @app/runtime)
                            "Could not capture a screen. Check screen recording permission and that the desktop is unlocked.")))]]]
            (letfn [(advance! [remaining]
                      (if-let [[name width height setup!] (first remaining)]
                        (do
                          (q/set-property! window "width" width)
                          (q/set-property! window "height" height)
                          (qt/later! 200
                            (fn []
                              (when setup! (setup!))
                              (qt/later! (if (or (str/starts-with? name "dialog-") (re-find #"(hover|pressed|focus)$" name)) 600 300)
                                #(do (snapshot! name) (advance! (rest remaining)))))))
                        (media-snapshots! output
                          #(do (reset! done? true)
                               (sessions/close! (:manager @app/runtime) 1)
                               (qt/quit!)))))]
              (qt/later! 300 #(advance! steps)))))
       :app-id "snitt-ui-parity" :keep-alive true :auto-quit-ms 30000)
      (when-not @done? (throw (ex-info "UI snapshots incomplete" {})))
      (println "PASS: Jolt editor snapshots")
      (finally (settings/clear!) (reset! app/runtime nil) (shutdown-agents)))))
