(ns snitt.preview-test
  "Inspect the actual Qt window pixels, including odd-sized crops at high DPI."
  (:require [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [snitt.app :as app]
            [snitt.document :as document]
            [snitt.geometry :as geometry]
            [snitt.native :as native]
            [snitt.settings :as settings]))

(def complete? (atom false))
(defn check! [condition message]
  (when-not condition (throw (ex-info message {}))))
(defn later [steps]
  (when-let [step (first steps)]
    (qt/later! 200 #(do (step) (later (rest steps))))))

(defn capture! [name]
  (let [window (:window @app/runtime)
        canvas (app/canvas)
        ratio (q/read-property canvas "previewPixelRatio")
        [cx cy] (q/map-point canvas (q/window-container window) 0 0)
        [x y w h] (:value (q/read-property canvas "imageRect"))
        dir (System/getenv "SNITT_PREVIEW_DIR")]
    (check! (== 1.0 (* ratio (q/read-property canvas "imageScale")))
            "Small images retain one source pixel per display pixel")
    (check! (q/window-snapshot window (str dir "/" name ".png")) "Window snapshot succeeded")
    (spit (str dir "/" name ".tsv")
          (str/join "\t" (map #(Math/round (* ratio %)) [(+ cx x) (+ cy y) w h])))))

(defn check-fit! []
  (let [window (:window @app/runtime)
        canvas (app/canvas)
        area (qt/find-by-id window "snitt-pointer")
        display (object/invoke! canvas "previewDisplay()" [])
        fit (geometry/editor-window
             {:available (:value (get display "available")) :pixel-ratio (get display "pixel-ratio")
              :image-width (q/read-property canvas "imageWidth")
              :image-height (q/read-property canvas "imageHeight")
              :chrome-width (- (q/read-property window "width") (q/read-property area "width"))
              :chrome-height (- (q/read-property window "height") (q/read-property area "height"))})]
    (doseq [key [:width :height :x :y]]
      (check! (== (key fit) (q/read-property window (name key)))
              (str "New source fits screen after toolbar layout: " key)))))

(defn exercise! []
  (check-fit!)
  (let [window (:window @app/runtime)
        canvas (app/canvas)
        manual-size (atom nil)]
    ;; Deliberately leave spare space; cuts must neither enlarge pixels nor undo a user's resize.
    (q/set-property! window "width" 1101)
    (q/set-property! window "height" 761)
    (later
     [(fn []
        (reset! manual-size (mapv #(q/read-property window %) ["width" "height"]))
        (capture! "original")
        (check! (document/edit! canvas {:kind :cut :vertical? true :from 101 :to 119}) "Vertical cut"))
      (fn []
        (capture! "vertical")
        (check! (document/edit! canvas {:kind :cut :vertical? false :from 79 :to 91}) "Horizontal cut"))
      (fn []
        (capture! "both")
        (check! (= @manual-size (mapv #(q/read-property window %) ["width" "height"]))
                "Editing preserves manual window size")
        (check! (document/undo! canvas) "Undo cut"))
      (fn []
        (capture! "undo")
        (check! (native/load-image! canvas (str (System/getenv "SNITT_PREVIEW_DIR") "/large.png"))
                "Larger source replaces the edited image"))
      (fn []
        (check-fit!)
        (let [ratio (q/read-property canvas "previewPixelRatio")
              area (qt/find-by-id window "snitt-pointer")]
          (when (and (>= (q/read-property area "width") (+ 32 (/ 1200 ratio)))
                     (>= (q/read-property area "height") (+ 32 (/ 700 ratio))))
            (check! (== 1.0 (* ratio (q/read-property canvas "imageScale")))
                    "Larger images use native pixels when the screen has room")))
        (reset! complete? true)
        (qt/quit!))])))

(defn -main [& _]
  (try
    (qt/run-application!
     #(do (app/start! :image (str (System/getenv "SNITT_PREVIEW_DIR") "/source.png"))
          (qt/later! 400 exercise!))
     :app-id "snitt-preview-test" :keep-alive true :auto-quit-ms 8000)
    (check! @complete? "Preview checks completed")
    (println "PASS: automatic window sizing, native pixel scale and stable cut/undo preview")
    (finally (settings/clear!) (reset! app/runtime nil) (shutdown-agents))))
