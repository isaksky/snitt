(ns snitt.arrangement-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [jolt.ffi :as ffi]
            [snitt.app :as app]
            [snitt.arrangement :as arrangement]
            [snitt.native :as native]
            [snitt.document :as document]
            [snitt.settings :as settings]))

(ffi/defcfn click-native "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn pointer-native "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)

(def checks (atom 0))
(def completed? (atom false))
(defn check! [value message]
  (swap! checks inc)
  (when-not value (throw (ex-info message {:state @app/state}))))
(defn id [name] (widget/find-by-id name))
(defn click! [name]
  (check! (= 1 (click-native (id name))) (str "Click " name)))
(defn pointer! [action x y]
  (check! (= 1 (pointer-native (id "snitt-pointer") action (double x) (double y)))
          (str "Pointer " action)))
(defn image-point [x y]
  (let [canvas (app/canvas)
        scale (q/read-property canvas "imageScale")]
    [(+ (/ (- (q/read-property canvas "width")
              (* (q/read-property canvas "imageWidth") scale)) 2) (* x scale))
     (+ (/ (- (q/read-property canvas "height")
              (* (q/read-property canvas "imageHeight") scale)) 2) (* y scale))]))
(defn later [steps]
  (when-let [step (first steps)]
    ;; Reference dialogs animate for 220ms; the next action starts after close.
    (qt/later! 300 #(do (step) (later (rest steps))))))
(defn layout [] (:arrangement @app/state))

(defn exercise! [paths wide-paths]
  (let [{:keys [window services context]} @app/runtime
        canvas (app/canvas)]
    (check! (object/invoke! (:root services) "loadRegionFilesForTest(QStringList)" [paths])
            "Captured regions enter the native buffer")
    (later
     [(fn []
        (check! (= (arrangement/captured 3) (layout)) "Capture initializes one Jolt arrangement")
        (check! (= 3 (:region-count (app/command-state context))) "Commands read Jolt count")
        (check! (= 184 (q/read-property canvas "imageWidth")) "Uneven grid width")
        (check! (= 182 (q/read-property canvas "imageHeight")) "Uneven grid height")
        (click! "columns-more"))
      (fn []
        (check! (= 3 (:columns (layout))) "Column control changes Jolt layout")
        (check! (= 3 (q/read-property canvas "columns")) "Native projection follows Jolt")
        (click! "columns-less")
        (click! "region-right"))
      (fn []
        (check! (= [1 0 2] (:order (layout))) "Keyboard reorder changes Jolt order")
        (check! (= 1 (:selected (layout))) "Reorder selects destination")
        (let [[x1 y1] (image-point 34 34)
              [x2 y2] (image-point 34 148)]
          (pointer! 0 x1 y1)
          (pointer! 1 x2 y2)
          (pointer! 2 x2 y2)))
      (fn []
        (check! (= [0 2 1] (:order (layout))) "Drag reorder uses the same Jolt model")
        (check! (= 2 (:selected (layout))) "Drag selects destination")
        (click! "arrange"))
      (fn []
        (check! (= :annotate (:phase (layout))) "Jolt enters annotation phase")
        (document/edit! canvas {:kind :text :start [24 24] :end [114 64]
                                :text "Edit" :font-size 20})
        (check! (q/read-property canvas "canUndo") "Annotation enters Jolt history")
        (widget/focus! (id "ink-good"))
        (app/dispatch! context "returnArrange"))
      (fn []
        (check! (widget/dialog-visible? (id "rearrange-dialog")) "Edits require confirmation")
        (check! (= :annotate (:phase (layout))) "Dialog does not discard edits")
        (click! "keep-editing")
        (check! (not (app/dispatch! context "returnArrange"))
                "Commands stay suppressed throughout the closing animation"))
      (fn []
        (check! (= :annotate (:phase (layout))) "Cancel keeps annotation phase")
        (check! (q/read-property canvas "canUndo") "Cancel keeps edits")
        (check! (q/read-property canvas "activeFocus") "Cancel restores canvas focus, including after toolbar focus")
        (app/dispatch! context "returnArrange"))
      (fn []
        (click! "discard-edits"))
      (fn []
        (check! (= :arrange (:phase (layout))) "Acceptance returns to Arrange")
        (check! (= [0 2 1] (:order (layout))) "Captured order survives discard")
        (check! (not (q/read-property canvas "canUndo")) "Only acceptance discards edits")
        (click! "region-remove"))
      (fn []
        (check! (= [0 2] (:order (layout))) "Removal updates Jolt order")
        (click! "region-remove"))
      (fn []
        (check! (= [0] (:order (layout))) "One captured region remains")
        (click! "region-remove"))
      (fn []
        (check! (nil? (layout)) "Removing last region clears Jolt arrangement")
        (check! (not (q/read-property canvas "hasImage")) "Last removal enters empty document")
        (check! (= "Open or paste an image" (:status @app/state)) "Empty flow is visible")
        (check! (object/invoke! (:root services) "loadRegionFilesForTest(QStringList)" [wide-paths])
                "Large regions load in a valid two-column layout"))
      (fn []
        (check! (= 2 (:columns (layout))) "Large capture starts with two columns")
        (click! "columns-more"))
      (fn []
        (check! (= 2 (:columns (layout))) "Failed composition rolls back Jolt columns")
        (check! (= 2 (q/read-property canvas "columns")) "Native projection also rolls back")
        (check! (= 22072 (q/read-property canvas "imageWidth")) "Visible image is unchanged")
        (check! (:error? @app/state) "Failure reports the layout limit")
        (reset! completed? true)
        (qt/quit!))])))

(defn -main [& _]
  (let [root (System/getenv "SNITT_JOLT_DATA_DIR")
        paths (mapv #(str root "/region-" % ".png") (range 3))
        wide-paths (mapv #(str root "/wide-" % ".png") (range 3))]
    (try
      (qt/run-application!
       #(do (app/start!) (qt/later! 250 #(exercise! paths wide-paths)))
       :app-id "snitt-jolt-arrangement-test" :keep-alive true :auto-quit-ms 8000)
      (check! @completed? "Arrangement sequence completed")
      (println "PASS:" @checks "Jolt arrangement controls, drag, confirmation and empty flow")
      (finally (settings/clear!) (reset! app/runtime nil) (shutdown-agents)))))
