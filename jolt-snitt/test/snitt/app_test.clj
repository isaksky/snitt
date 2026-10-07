(ns snitt.app-test
  (:require [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as widget]
            [jolt.ffi :as ffi]
            [snitt.app :as app]
            [snitt.native :as native]
            [snitt.sessions :as sessions]
            [snitt.settings :as settings]))

(ffi/defcfn click-native "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn key-native "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn pointer-native "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(ffi/defcfn file-result-native "gqt_test_file_result" [:uint64 :pointer] :int :blocking)

(def checks (atom 0))
(def completed? (atom false))
(def saved-path (atom nil))
(def initial-canvas (atom nil))
(def capture-starts (atom []))

(defn check! [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {:state @app/state}))))

(defn id [name] (widget/find-by-id name))
(defn enabled? [name] (q/read-property (id name) "enabled"))
(defn click! [name] (check! (= 1 (click-native (id name))) (str "Click " name)))
(defn key! [window key modifiers]
  (check! (= 1 (key-native window key modifiers)) (str "Key " key)))
(defn pointer! [action x y]
  (check! (= 1 (pointer-native (id "snitt-pointer") action (double x) (double y)))
          (str "Pointer " action)))
(defn file-result! [name]
  (ffi/with-c-string [path name]
    (check! (= 1 (file-result-native (id "open-file") path)) "File chooser accepted")))

(defn step! [steps]
  (when-let [f (first steps)]
    (qt/later! 120 #(do (f) (step! (rest steps))))))

(defn image-point [x y]
  (let [handle (app/canvas)
        scale (q/read-property handle "imageScale")
        left (/ (- (q/read-property handle "width") (* 800 scale)) 2)
        top (/ (- (q/read-property handle "height") (* 800 scale)) 2)]
    [(+ left (* x scale)) (+ top (* y scale))]))

(defn exercise! [image]
  (let [{:keys [window services]} @app/runtime
        canvas (app/canvas)
        {:keys [root settings backend trim]} services
        directory (System/getenv "SNITT_JOLT_DATA_DIR")
        output-file (System/getenv "SNITT_JOLT_SAVED_PATH")]
    (reset! initial-canvas canvas)
    (check! (qt/window-alive? window) "Jolt owns a live editor window")
    (check! (every? q/alive? [canvas root settings backend trim])
            "Canvas and services share the Qt session")
    (check! (.startsWith (q/read-property settings "settingsFile") directory)
            "Development settings stay in isolated data directory")
    (check! (some? @settings/published) "Jolt validated and published initial settings")
    (check! (not-any? enabled? ["undo" "redo" "copy" "save" "rect"])
            "Document commands start disabled")
    (check! (not (q/read-property (id "snitt-pointer") "enabled"))
            "Pointer input starts disabled")
    (step!
     [(fn []
        (click! "open")
        (check! (widget/dialog-visible? (id "open-file")) "Open button opens native chooser")
        (file-result! image))
      (fn []
        (check! (q/read-property canvas "hasImage") "Chooser loads generated image")
        (check! (every? enabled? ["copy" "save" "rect"])
                "Image notification enables document commands")
        (check! (not-any? enabled? ["undo" "redo"]) "New image has no history")
        (check! (q/read-property (id "snitt-pointer") "enabled")
                "Loaded image enables pointer input")
        (check! (= (q/read-property canvas "width")
                   (q/read-property (id "snitt-pointer") "width"))
                "Canvas follows pointer area width")
        (click! "arrow"))
      (fn []
        (check! (= "arrow" (q/read-property canvas "tool")) "Arrow button selects native tool")
        (click! "rect"))
      (fn []
        (check! (= "rect" (q/read-property canvas "tool")) "Rectangle button selects native tool")
        (check! (= canvas (app/canvas)) "Tool rerender keeps the same canvas")
        (let [[x1 y1] (image-point 200 200)
              [x2 y2] (image-point 400 400)]
          (pointer! 0 x1 y1)
          (pointer! 1 x2 y2)
          (pointer! 2 x2 y2)))
      (fn []
        (check! (q/read-property canvas "canUndo") "Pointer drag enters native history")
        (check! (enabled? "undo") "History notification enables Undo")
        (check! (= canvas (app/canvas)) "History rerender keeps native document")
        (key! window 90 67108864))
      (fn []
        (check! (not (q/read-property canvas "canUndo")) "Keyboard Undo removes annotation")
        (check! (enabled? "redo") "Undo notification enables Redo")
        (check! (not (enabled? "undo")) "Undo button disables at start of history")
        (key! window 90 100663296))
      (fn []
        (check! (q/read-property canvas "canUndo") "Keyboard Redo restores annotation")
        (check! (enabled? "undo") "Redo enables Undo")
        (click! "copy")
        (check! (= "Copied to clipboard" (:status @app/state))
                "Copy button publishes edited image"))
      (fn []
        (let [clip (q/clipboard-image @widget/session)]
          (try
            (check! (pos? clip) "Clipboard contains image pixels")
            (q/image-save @widget/session clip (System/getenv "SNITT_JOLT_COPIED_PATH"))
            (finally (q/image-dispose @widget/session clip))))
        (click! "save"))
      (fn []
        (let [path (:status @app/state)]
          (check! (.startsWith path directory) "Save button writes to isolated Pictures root")
          (let [saved (q/image-load @widget/session path)]
            (check! (pos? saved) "Saved PNG can be reopened")
            (q/image-dispose @widget/session saved))
          (reset! saved-path path)
          (spit output-file path))
        (spit (str directory "/corrupt.png") "This is not a PNG")
        (click! "open")
        (file-result! (str directory "/corrupt.png")))
      (fn []
        (check! (q/read-property canvas "hasImage") "Failed load preserves image")
        (check! (q/read-property canvas "canUndo") "Failed load preserves history")
        (check! (str/includes? (:status @app/state) "Could not open")
                "Failed load displays an actionable error")
        (check! (str/includes? (q/read-property (id "status") "text") "Could not open")
                "Failed load error reaches visible status label")
        (let [blocked (str directory "/blocked-save")]
          (spit blocked "occupied")
          (q/set-property! canvas "saveRoot" blocked)
          (click! "save")))
      (fn []
        (check! (q/read-property canvas "hasImage") "Failed save preserves image")
        (check! (q/read-property canvas "canUndo") "Failed save preserves history")
        (check! (str/includes? (:status @app/state) "Could not")
                "Failed save displays an actionable error")
        (check! (str/includes? (q/read-property (id "status") "text") "Could not")
                "Failed save error reaches visible status label")
        (q/set-property! canvas "saveRoot"
                         (q/read-property settings "picturesRoot"))
        (with-redefs [native/copy! (fn [_] false)]
          (click! "copy")))
      (fn []
        (check! (q/read-property canvas "canUndo") "Failed copy preserves history")
        (check! (str/includes? (:status @app/state) "Could not copy")
                "Failed copy displays retry guidance")
        (check! (str/includes? (q/read-property (id "status") "text") "Could not copy")
                "Failed copy error reaches visible status label")
        (check! (= @initial-canvas (app/canvas))
                "Error rerender keeps the canvas and document")
        (let [manager (:manager @app/runtime)
              second-id (sessions/open-image! manager (str "file://" image) "second.png")
              second (sessions/session manager second-id)
              second-canvas (app/canvas (:context second))]
          (check! (= 2 (count (sessions/sessions manager))) "Two Jolt editor sessions coexist")
          (check! (not= canvas second-canvas) "Each editor owns a distinct native document")
          (check! (not= backend (get-in second [:services :backend]))
                  "Each editor owns a distinct native backend")
          (check! (q/read-property second-canvas "hasImage") "Second image opens independently")
          (let [settings-path (q/read-property settings "settingsFile")
                changed (-> (slurp settings-path)
                            (str/replace "good=#22c55e" "good=#426ab3")
                            (str/replace "toolRectangle=R" "toolRectangle=F6"))]
            (spit settings-path changed)
            (check! (settings/reload!) "Jolt accepts a live settings change")
            (check! (= "#ff426ab3" (:value (q/read-property canvas "goodColor")))
                    "First editor receives live color")
            (check! (= "#ff426ab3" (:value (q/read-property second-canvas "goodColor")))
                    "Second editor receives live color")
            (check! (= ["F6"] (get-in @settings/published [:shortcuts "toolRectangle"]))
                    "Jolt publishes new shortcut"))
          (native/begin! second-canvas 200 200)
          (native/move! second-canvas 400 400)
          (native/end! second-canvas 400 400)
          (check! (q/read-property second-canvas "canUndo") "Second image records its own edit")
          (check! (q/read-property canvas "canUndo") "First image retains its own history")
          (sessions/active! manager second-id)
          (qt/minimize! window)
          (let [first-visibility (q/read-property window "visibility")
                second-visibility (q/read-property (:window second) "visibility")
                capture-id (sessions/capture! manager false false)]
            (check! (= 1 (count @capture-starts)) "One native capture admitted")
            (check! (= capture-id (:capture (sessions/snapshot manager)))
                    "Capture identity is tracked in Jolt")
            (check! (nil? (sessions/capture! manager false false))
                    "Concurrent capture is rejected")
            (check! (= 3 (count (sessions/sessions manager)))
                    "Rejected capture creates no additional editor")
            (check! (not (q/read-property window "visible")) "First editor hidden for capture")
            (check! (not (q/read-property (:window second) "visible"))
                    "Second editor hidden for capture")
            (sessions/complete-capture! manager capture-id false)
            (check! (= first-visibility (q/read-property window "visibility"))
                    "Minimized editor visibility restored exactly")
            (check! (= second-visibility (q/read-property (:window second) "visibility"))
                    "Visible editor restored exactly")
            (check! (= 2 (count (sessions/sessions manager)))
                    "Canceled capture destroys only its new session"))
          (qt/hide! (:window second))
          (let [capture-id (sessions/capture! manager false false)
                captured (sessions/session manager capture-id)
                captured-canvas (app/canvas (:context captured))]
            (check! (= 2 (count @capture-starts)) "Next capture admitted after cancellation")
            (check! (native/load-image! captured-canvas image)
                    "Captured image loads into only the new document")
            (sessions/complete-capture! manager capture-id true)
            (check! (q/read-property (:window captured) "visible")
                    "Successful capture activates its editor")
            (check! (not (q/read-property (:window second) "visible"))
                    "Previously hidden editor stays hidden")
            (check! (q/read-property canvas "canUndo")
                    "Capture leaves first document history intact")
            (sessions/close! manager capture-id))
          (sessions/close! manager second-id)
          (check! (not (q/alive? (get-in second [:services :backend])))
                  "Closing second editor invalidates only its backend")
          (sessions/complete-capture! manager second-id true)
          (check! (= 1 (count (sessions/sessions manager)))
                  "Stale completion cannot affect the surviving editor")
          (sessions/show-editor! manager)
          (check! (q/read-property canvas "hasImage") "First document survives second close")
          (check! (q/read-property canvas "canUndo") "First history survives second close")
          (native/undo! canvas)
          (check! (q/read-property canvas "canRedo") "Surviving editor remains editable")
          (native/redo! canvas)
          (check! (q/read-property canvas "canUndo") "Surviving editor can redo its edit"))
        (qt/later! 120
          (fn []
            (check! (str/includes? (q/read-property (id "rect") "text") "F6")
                    "Live shortcut hint reaches first editor toolbar")
            (when-let [icon (:tray @app/runtime)]
              (when (q/alive? icon)
                (check! (str/includes? (q/read-property icon "toolTip")
                                      (get-in @settings/published [:hints "globalCapture"]))
                        "Tray hint follows the published shortcut")))
            (sessions/close! (:manager @app/runtime) 1)
            (check! (not (q/alive? settings)) "Disposal invalidates settings")
            (check! (not (q/alive? backend)) "Disposal invalidates backend")
            (check! (not (q/alive? trim)) "Disposal invalidates trim")
            (reset! completed? true)
            (qt/close-window! window))))])))

(defn -main [& _]
  (let [image (System/getenv "SNITT_JOLT_IMAGE")]
    (try
      (qt/run-application!
       #(do (app/start! :capture-start (fn [_ id multiple video]
                                        (swap! capture-starts conj [id multiple video])))
            (qt/later! 250 #(exercise! image)))
       :app-id "snitt-jolt-development" :keep-alive false :auto-quit-ms 8000)
      (check! @completed? "Editor exercised before event loop exit")
      (check! (zero? (widget/live-count)) "Managed widgets released")
      (check! (zero? (q/owned-count)) "Native objects released")
      (println "PASS:" @checks "Snitt Jolt UI checks; exported" @saved-path)
      (finally (reset! app/runtime nil) (shutdown-agents)))))
