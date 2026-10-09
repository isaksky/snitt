(ns snitt.annotation-test
  "Real Qt input through the Jolt annotation toolbar and draft overlay."
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [jolt.ffi :as ffi]
            [snitt.app :as app]
            [snitt.sessions :as sessions]
            [snitt.settings :as settings]))

(ffi/defcfn click-native "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn key-native "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn pointer-native "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(ffi/defcfn paste-native "gqt_test_paste" [:uint64 :pointer] :int :blocking)

(def checks (atom 0))
(def complete? (atom false))
(defn check! [value message]
  (swap! checks inc)
  (when-not value (throw (ex-info message {:state @app/state}))))
(defn id [name] (or (widget/find-by-id name)
                    (throw (ex-info "Missing annotation control" {:id name}))))
(defn p [name property] (q/read-property (id name) property))
(defn click! [name] (check! (= 1 (click-native (id name))) (str "Click " name)))
(defn pointer! [name action x y]
  (check! (= 1 (pointer-native (id name) action (double x) (double y)))
          (str "Pointer " name " " action)))
(defn paste-text! [window text]
  (ffi/with-c-string [s text]
    (check! (= 1 (paste-native window s)) "Keyboard paste reaches text editor")))
(defn key! [window key modifiers]
  (check! (= 1 (key-native window key modifiers)) (str "Key " key)))
(defn later [steps]
  (when-let [step (first steps)]
    (qt/later! 180 #(do (step) (later (rest steps))))))
(defn image-point [x y]
  (let [canvas (app/canvas)
        scale (q/read-property canvas "imageScale")]
    [(+ (/ (- (p "snitt-pointer" "width")
              (* (q/read-property canvas "imageWidth") scale)) 2) (* x scale))
     (+ (/ (- (p "snitt-pointer" "height")
              (* (q/read-property canvas "imageHeight") scale)) 2) (* y scale))]))
(defn start-draft! []
  (click! "tool-text")
  (qt/later! 60
    (fn []
      (let [[x y] (image-point 200 180)]
        (pointer! "snitt-pointer" 0 x y)
        (pointer! "snitt-pointer" 2 x y)))))
(defn capture! [path]
  (check! (q/save-window @widget/session path) (str "Capture " path)))

(defn exercise! [window image capture-dir]
  (let [canvas (app/canvas)
        draft-context (:context @app/runtime)
        wide (str capture-dir "/annotation-wide-2x.png")
        narrow (str capture-dir "/annotation-narrow-2x.png")]
    (check! (q/read-property canvas "hasImage") "Generated image loaded")
    (doseq [name ["tool-cut" "rect" "tool-highlight" "tool-text" "arrow"
                  "tool-blur" "tool-erase" "undo" "redo" "ink-good" "ink-bad"
                  "save" "copy"]]
      (check! (id name) (str name " is reachable")))
    (check! (= "Rectangle" (p "rect" "Accessible.name")) "Tool has accessible name")
    (check! (= "Good mode" (p "ink-good" "Accessible.name")) "Mode has accessible name")
    (check! (p "rect" "checked") "Current tool is checked")
    (doseq [[tool action] [["cut" "toolCut"] ["rect" "toolRectangle"]
                           ["highlight" "toolHighlight"] ["text" "toolText"]
                           ["arrow" "toolArrow"] ["blur" "toolPixelate"]
                           ["erase" "toolErase"]]]
      (check! (= (get-in @settings/published [:hints action])
                 (p (str "shortcut-" tool) "text"))
              (str tool " shows its current keyboard hint")))
    (check! (= 1100 (q/read-property window "width")) "Reference editor width")
    (check! (= 760 (q/read-property window "height")) "Reference editor height")
    (check! (= 860 (q/read-property window "minimumWidth")) "Reference minimum width")
    (check! (= 360 (q/read-property window "minimumHeight")) "Compact editor minimum height")
    (check! (= 26 (p "rect" "icon.width")) "Original icon dimensions")
    (check! (= 12 (p "shortcut-rect" "font.pixelSize")) "Original caption dimensions")
    (check! (== 168 (p "mode-segmented-control" "width")) "Original segmented control width")
    (check! (== 36 (p "editor-footer" "height")) "Original preview footer height")
    (check! (not (p "rect" "checkable")) "Tool state is controlled by Jolt, not a toggle checkbox")
    (check! (= (q/read-property canvas "goodColor") (p "ink-good" "actionColor"))
            "Good mode uses the configured ink color")
    (check! (= (q/read-property canvas "badColor") (p "ink-bad" "actionColor"))
            "Bad mode uses the configured ink color")
    (capture! wide)
    (click! "ink-good")
    (let [wide-height (p "annotation-toolbar" "height")]
      (q/set-property! window "width" 860)
      (later
       [(fn []
          (check! (p "ink-good" "checked") "Selected color mode follows the Jolt projection")
          (check! (> (p "annotation-toolbar" "height") wide-height)
                  "Toolbar wraps whole groups at narrow width")
          (doseq [name ["tool-cut" "rect" "tool-highlight" "tool-text" "arrow"
                        "tool-blur" "tool-erase" "ink-good" "ink-bad" "save" "copy"]]
            (check! (p name "enabled") (str name " remains enabled when narrow")))
          (capture! narrow)
          (start-draft!))
        (fn []
          (check! (:draft @app/state) "Canvas text gesture starts an uncommitted draft")
          (let [{:keys [x y width]} (:draft @app/state)
                scale (q/read-property canvas "imageScale")]
            (check! (< (abs (- (p "annotation-text-frame" "x")
                              (+ (/ (- (p "snitt-pointer" "width") (* 800 scale)) 2)
                                 (* x scale)))) 0.5)
                    "Draft x follows high-DPI logical image geometry")
            (check! (< (abs (- (p "annotation-text-frame" "width") (* width scale))) 0.5)
                    "Draft width follows high-DPI logical image geometry"))
          (check! (p "annotation-text" "activeFocus") "Draft receives keyboard focus")
          (check! (= "Annotation text" (p "annotation-text" "Accessible.name"))
                  "Draft has accessible name")
          (check! (not (p "undo" "enabled")) "Undo is disabled during draft")
          (doseq [name ["tool-text" "ink-good" "ink-bad" "save" "copy"]]
            (check! (p name "enabled") (str name " can commit and act on a draft")))
          (paste-text! window "A😀日本\né line")
          (check! (= "A😀日本\né line" (:text (:draft @app/state)))
                  "Unicode multiline paste updates only draft")
          (object/invoke! (id "annotation-text") "select(int,int)" [1 3])
          (check! (= "😀" (p "annotation-text" "selectedText"))
                  "Unicode selection spans surrogate pair")
          (swap! app/state update :context-revision (fnil inc 0)))
        (fn []
          (check! (= "😀" (p "annotation-text" "selectedText"))
                  "Unrelated rerender preserves selection")
          (check! (p "annotation-text" "activeFocus") "Rerender retains focus")
          (paste-text! window (apply str (repeat 35 "Unicode 🦊 日本 line\n"))))
        (fn []
          (let [viewport (q/child-target (id "annotation-text-scroll") "contentItem")]
            (check! (> (p "annotation-text" "contentHeight")
                       (q/read-property viewport "height"))
                    "Long Unicode draft wraps beyond viewport")
            (q/set-property! viewport "contentY" 80)
            (check! (pos? (q/read-property viewport "contentY"))
                    "Draft scrolls"))
          (let [before (q/read-property canvas "textSize")
                placement (select-keys (:draft @app/state) [:x :y])]
            (pointer! "annotation-text-scroll" 5 10 10)
            (check! (> (q/read-property canvas "textSize") before)
                    (str "Wheel adjusts annotation font size from " before
                         " to " (q/read-property canvas "textSize")))
            (check! (= placement (select-keys (:draft @app/state) [:x :y]))
                    "Font wheel preserves image placement")))
        (fn []
          (let [before (:width (:draft @app/state))]
            (pointer! "annotation-text-resize-handle" 0 8 15)
            (pointer! "annotation-text-resize-handle" 1 70 15)
            (pointer! "annotation-text-resize-handle" 2 70 15)
            (check! (> (:width (:draft @app/state)) before)
                    "Real drag widens draft in image coordinates")))
        (fn []
          (check! (p "annotation-text" "activeFocus") "Resize restores editor focus")
          (capture! (str capture-dir "/annotation-draft-resized-2x.png"))
          (key! window 16777216 0))
        (fn []
          (check! (nil? (:draft @app/state)) "Escape cancels draft")
          (check! (not (q/read-property canvas "canUndo"))
                  "Cancel creates no document operation")
          (check! (q/read-property canvas "activeFocus") "Cancel restores canvas focus")
          (start-draft!))
        (fn []
          (paste-text! window "Placed 🦊\n日本")
          (key! window 16777220 67108864))
        (fn []
          (check! (nil? (:draft @app/state)) "Ctrl+Enter commits draft")
          (check! (q/read-property canvas "canUndo") "Commit creates document history")
          (check! (q/read-property canvas "activeFocus") "Commit restores canvas focus")
          (start-draft!))
        (fn []
          (paste-text! window "Switch tool")
          (click! "arrow")
          (check! (nil? (:draft @app/state)) "Tool switch commits draft"))
        (fn []
          (check! (= "arrow" (q/read-property canvas "tool")) "Selected tool reaches canvas")
          (start-draft!))
        (fn []
          (paste-text! window "Outside click")
          (pointer! "snitt-pointer" 0 5 5)
          (pointer! "snitt-pointer" 2 5 5)
          (check! (nil? (:draft @app/state)) "Click outside commits draft")
          (start-draft!))
        (fn []
          (paste-text! window "Keep on failed paste")
          (q/clipboard-publish-text @widget/session "No image")
          (check! (not (app/paste-image! draft-context)) "Text clipboard cannot replace image")
          (check! (= "Keep on failed paste" (:text (:draft @app/state)))
                  "Failed paste preserves draft")
          (let [source (q/image-load @widget/session image)]
            (try
              (q/clipboard-publish-image @widget/session source)
              (finally (q/image-dispose @widget/session source))))
          (check! (app/paste-image! draft-context) "Image paste succeeds")
          (check! (nil? (:draft @app/state)) "Successful paste discards draft")
          (check! (q/read-property canvas "hasImage") "Replacement document remains loaded")
          (sessions/close! (:manager @app/runtime) 1)
          (reset! complete? true)
          (qt/quit!))]))))

(defn -main [& _]
  (try
    (qt/run-application!
     #(do (app/start! :image (System/getenv "SNITT_JOLT_IMAGE"))
          (qt/later! 300
            (fn [] (let [window (:window @app/runtime)]
               ;; Explicit wide/narrow fixtures; new sources normally size to the screen.
               (q/set-property! window "minimumWidth" 860)
               (q/set-property! window "minimumHeight" 360)
               (q/set-property! window "width" 1100)
               (q/set-property! window "height" 760)
               (qt/later! 100 #(exercise! window (System/getenv "SNITT_JOLT_IMAGE")
                                                (System/getenv "SNITT_JOLT_CAPTURE_DIR")))))))
     :app-id "snitt-jolt-annotation-test" :keep-alive true :auto-quit-ms 10000)
    (check! @complete? "Annotation interaction sequence completed")
    (println "PASS:" @checks "annotation input, draft, layout, accessibility and 2× captures")
    (finally (settings/clear!) (reset! app/runtime nil) (shutdown-agents))))
