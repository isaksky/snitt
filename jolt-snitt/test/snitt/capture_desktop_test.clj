(ns snitt.capture-desktop-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.sessions :as sessions]))

(def completed? (atom false))

(defn check! [condition message]
  (when-not condition (throw (ex-info message {}))))

(defn root [manager id]
  (get-in (sessions/session manager id) [:services :root]))

(defn invoke! [manager id signature args]
  (object/invoke! (root manager id) signature args))

(defn await-selectors! [manager id expected action attempts]
  (qt/later! 100
    (fn []
      (let [active (:capture (sessions/snapshot manager))
            count (when (= active id)
                    (invoke! manager id "captureSelectorCountForTest()" []))]
        (cond
          (= count expected) (action)
          (not= active id) (throw (ex-info "Desktop capture ended before selection" {}))
          (>= attempts 120) (throw (ex-info "Timed out waiting for native selectors"
                                          {:count count :expected expected}))
          :else (await-selectors! manager id expected action (inc attempts)))))))

(defn exercise! []
  (let [manager (:manager @app/runtime)
        host (get-in @app/runtime [:host :root])
        displays (object/invoke! host "captureDisplaysForTest()" [])
        count (count displays)
        scales (mapv #(get % "scale") displays)]
    (check! (pos? count) "Actual macOS desktop has a display")
    (let [single (sessions/capture! manager false false)]
      (await-selectors! manager single count
        (fn []
          (check! (invoke! manager single "selectCaptureRegionForTest(int)" [0])
                  "Native region selector cropped an actual display")
          (qt/later! 200
            (fn []
              (check! (nil? (:capture (sessions/snapshot manager)))
                      "Jolt completed actual single-display routing")
              (let [captured (sessions/session manager single)
                    canvas (app/canvas (:context captured))
                    width (q/read-property canvas "imageWidth")
                    expected (* 100 (first scales))]
                (check! (q/read-property canvas "hasImage")
                        "Actual screen pixels reached the new document")
                (check! (<= (Math/abs (- width expected)) 2)
                        "Native crop respects actual desktop pixel scale"))
              (sessions/close! manager single)
              (let [multiple (sessions/capture! manager true false)]
                (await-selectors! manager multiple count
                  (fn []
                    (doseq [monitor (range count)]
                      (check! (invoke! manager multiple "addCaptureRegionForTest(int)" [monitor])
                              "Captured a region from each actual display"))
                    (when (= count 1)
                      (check! (invoke! manager multiple "addCaptureRegionForTest(int)" [0])
                              "Captured a second region on the actual display"))
                    (check! (invoke! manager multiple "acceptCaptureRegionsForTest()" [])
                            "Native selector accepted actual regions")
                    (qt/later! 200
                      (fn []
                        (check! (nil? (:capture (sessions/snapshot manager)))
                                "Jolt completed actual multiple-region routing")
                        (let [captured (sessions/session manager multiple)
                              canvas (app/canvas (:context captured))]
                          (check! (= (max count 2) (q/read-property canvas "regionCount"))
                                  "Actual regions reached one new document"))
                        (sessions/close! manager multiple)
                        (println "PASS: actual macOS capture; displays" count "scales" scales)
                        (when (= count 1)
                          (println "UNAVAILABLE: actual multi-monitor validation needs another connected display"))
                        (when-not (some #(< 1 %) scales)
                          (println "UNAVAILABLE: actual high-DPI validation needs a scaled display"))
                        (reset! completed? true)
                        (qt/quit!))))
                  0)))))
        0))))

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
