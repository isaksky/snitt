(ns snitt.capture-desktop-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.region :as region]
            [snitt.sessions :as sessions]
            [jolt.ffi :as ffi]))

(ffi/defcfn pointer! "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(ffi/defcfn key! "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn click! "gqt_test_click" [:uint64] :int :blocking)
(def completed? (atom false))

(defn check! [condition message]
  (when-not condition (throw (ex-info message {}))))

(defn root [manager id] (get-in (sessions/session manager id) [:services :root]))
(defn screens [manager id]
  (object/invoke! (root manager id) "capturePickerScreens()" []))

(defn await-picker! [manager id expected action attempts]
  (qt/later! 100
    (fn []
      (let [active (:capture (sessions/snapshot manager))
            picker (:picker (sessions/snapshot manager))
            count (when (and (= active id) picker) (count @(:windows picker)))]
        (cond
          (= count expected) (action picker)
          (not= active id) (throw (ex-info "Desktop capture ended before selection" {}))
          (>= attempts 120) (throw (ex-info "Timed out waiting for Glimmer picker"
                                          {:count count :expected expected}))
          :else (await-picker! manager id expected action (inc attempts)))))))

(defn region-point [screen]
  [(max 12 (long (- (/ (get screen "width") 2) 50)))
   (max 100 (long (- (/ (get screen "height") 2) 50)))])

(defn drag! [picker monitor x y]
  (let [input (qt/find-by-id (nth @(:windows picker) monitor) (str "picker-input-" monitor))]
    (check! (some? input) "Actual monitor has Glimmer input")
    (doseq [[action px py] [[0 x y] [1 (+ x 100) (+ y 100)] [2 (+ x 100) (+ y 100)]]]
      (check! (= 1 (pointer! input action (double px) (double py)))
              "Real input reaches capture region"))))

(defn finish-cancel! [manager id count scales]
  (check! (nil? (:capture (sessions/snapshot manager))) "Cancel ends actual capture")
  (check! (nil? (sessions/session manager id)) "Canceled editor disposed")
  (check! (= 0 (qt/window-count)) "All monitor overlays disposed")
  (println "PASS: actual desktop Glimmer capture; displays" count "scales" scales)
  (when (= count 1)
    (println "UNAVAILABLE: actual multi-monitor validation needs another connected display"))
  (when-not (some #(< 1 %) scales)
    (println "UNAVAILABLE: actual high-DPI validation needs a scaled display"))
  (reset! completed? true)
  (qt/quit!))

(defn start-cancel! [manager count scales]
  (let [id (sessions/capture! manager false false)]
    (await-picker! manager id count
      (fn [picker]
        (check! (= 1 (key! (first @(:windows picker)) 16777216 0))
                "Escape reaches Jolt picker")
        (qt/later! 150 #(finish-cancel! manager id count scales)))
      0)))

(defn finish-multiple! [manager id count scales]
  (check! (nil? (:capture (sessions/snapshot manager)))
          "Multiple capture completed through Jolt picker")
  (let [canvas (app/canvas (:context (sessions/session manager id)))]
    (check! (= (max count 2) (q/read-property canvas "regionCount"))
            "All actual crops reached one document"))
  (sessions/close! manager id)
  (start-cancel! manager count scales))

(defn start-multiple! [manager count scales]
  (let [id (sessions/capture! manager true false)]
    (await-picker! manager id count
      (fn [picker]
        (let [captured-screens (screens manager id)]
          (doseq [monitor (range count)]
            (let [[x y] (region-point (nth captured-screens monitor))]
              (drag! picker monitor x y)))
          (when (= count 1)
            (let [[x y] (region-point (first captured-screens))]
              (drag! picker 0 (+ x 110) y)))
          (check! (= (max count 2) (clojure.core/count (:regions @(:state picker))))
                  "Ordered selections span captured displays")
          (qt/later! 150
            (fn []
              (let [toolbar (qt/find-by-id (first @(:windows picker)) "picker-toolbar-0")
                    arrange (q/child-target toolbar "arrangeButton")]
                (check! (some? arrange) "Arrange action is visible")
                (check! (= 1 (click! arrange)) "Real Arrange click reaches Jolt"))
              (qt/later! 200 #(finish-multiple! manager id count scales))))))
      0)))

(declare start-single!)

(defn finish-single! [manager id screen area monitor count scales]
  (check! (nil? (:capture (sessions/snapshot manager)))
          "Single capture completed through Jolt picker")
  (let [canvas (app/canvas (:context (sessions/session manager id)))
        [_ _ expected-width _] (region/pixel-rect area screen)]
    (check! (q/read-property canvas "hasImage") "Actual screen pixels reached document")
    (check! (= expected-width (q/read-property canvas "imageWidth"))
            (str "Native crop matches display-local pixel conversion on monitor " monitor)))
  (sessions/close! manager id)
  (if (< (inc monitor) count)
    (start-single! manager (inc monitor) count scales)
    (start-multiple! manager count scales)))

(defn start-single! [manager monitor count scales]
  (let [id (sessions/capture! manager false false)]
    (await-picker! manager id count
      (fn [picker]
        (let [screen (nth (screens manager id) monitor)
              [x y] (region-point screen)
              image (qt/find-by-id (nth @(:windows picker) monitor)
                                   (str "picker-background-" monitor))]
          (check! (= "ready" (q/read-property image "loadState"))
                  (str "Actual frozen desktop is visible on monitor " monitor))
          (drag! picker monitor x y)
          (qt/later! 200
            #(finish-single! manager id screen [x y 100 100] monitor count scales))))
      0)))

(defn exercise! []
  (let [manager (:manager @app/runtime)
        host (get-in @app/runtime [:host :root])
        displays (object/invoke! host "captureDisplaysForTest()" [])
        count (count displays)
        scales (mapv #(get % "scale") displays)]
    (check! (pos? count) "Actual desktop has a display")
    (start-single! manager 0 count scales)))

(defn -main [& _]
  (try
    (qt/run-application!
     #(do (app/start! :background true)
          (qt/later! 250 exercise!))
     :app-id "snitt-jolt-development" :keep-alive true :auto-quit-ms 16000)
    (check! @completed? "Actual desktop capture sequence completed")
    (check! (zero? (widget/live-count)) "Managed widgets released")
    (check! (zero? (q/owned-count)) "Native objects released")
    (finally (reset! app/runtime nil) (shutdown-agents))))
