(ns snitt.gesture-test
  "Compare Jolt gesture traces and native results with the retained Qt canvas."
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.geometry :as geometry]
            [snitt.native :as native]
            [snitt.settings :as settings]))

(def checks (atom 0))
(def complete? (atom false))
(defn check! [ok description]
  (swap! checks inc)
  (when-not ok (throw (ex-info description {}))))

(defn- apply-effects! [canvas effects]
  (doseq [effect effects]
    (case (:op effect)
      :preview (native/set-preview! canvas effect)
      :clear-preview (native/clear-preview! canvas)
      :draw (native/draw! canvas effect))))

(defn- export! [canvas path]
  (check! (native/copy! canvas) "Canvas exports image")
  (let [image (q/clipboard-image @widget/session)]
    (try
      (check! (q/image-save @widget/session image path) "Clipboard PNG saved")
      (finally (q/image-dispose @widget/session image)))))

(defn- exercise! [path directory]
  (let [migrated (app/canvas)
        reference (widget/create! :snitt/canvas {:width 832 :height 832})]
    (try
      (doseq [[zoom tool start end] [[1 "rect" [200 200] [400 400]]
                                     [3 "arrow" [120 160] [510 450]]
                                     [1 "highlight" [210 230] [420 490]]
                                     [3 "blur" [250 250] [500 510]]
                                     [1 "erase" [240 260] [430 420]]
                                     [3 "cut" [250 100] [370 190]]
                                     [1 "cut" [100 250] [190 370]]
                                     [3 "cut" [250.5 100] [370.5 190]]
                                     [1 "cut" [190 370] [100 250]]]]
        (let [size (+ 32 (* 800 zoom))]
          (q/set-property! migrated "width" size)
          (q/set-property! migrated "height" size)
          (q/set-property! reference "width" size)
          (q/set-property! reference "height" size)
          (check! (native/load-image! migrated path) "Migrated image reloads")
          (check! (native/load-image! reference path) "Reference image reloads")
          (q/set-property! migrated "tool" tool)
          (q/set-property! reference "tool" tool)
          (let [image (geometry/image-geometry {:width size :height size
                                                :image-width 800 :image-height 800})
                state (first (geometry/select-tool (geometry/initial-state) tool))
                [x1 y1] (geometry/view-point image start)
                [x2 y2] (geometry/view-point image end)
                [pressed begin] (geometry/press state image x1 y1)
                [moved previews] (geometry/drag pressed image x2 y2)
                [finished commands] (geometry/release moved image x2 y2)]
            (native/set-geometry! migrated image)
            (apply-effects! migrated begin)
            (native/begin! reference x1 y1)
            (apply-effects! migrated previews)
            (native/move! reference x2 y2)
            (check! (and (not (q/read-property migrated "canUndo"))
                         (not (q/read-property reference "canUndo")))
                    "Previews never enter document history")
            (apply-effects! migrated commands)
            (native/end! reference x2 y2)
            (check! (nil? (:gesture finished)) "Jolt releases gesture")
            (check! (= (q/read-property migrated "canUndo")
                       (q/read-property reference "canUndo"))
                    "Both paths commit the same history")
            (check! (= [(q/read-property migrated "imageWidth")
                        (q/read-property migrated "imageHeight")]
                       [(q/read-property reference "imageWidth")
                        (q/read-property reference "imageHeight")])
                    "Cut dimensions match reference")
            (export! migrated (str directory "/migrated-" zoom "-" tool "-" (first start) ".png"))
            (export! reference (str directory "/reference-" zoom "-" tool "-" (first start) ".png")))))
      (let [image (geometry/image-geometry {:width 832 :height 832
                                            :image-width 800 :image-height 800})
            [x y] (geometry/view-point image [100 100])
            _ (check! (native/load-image! migrated path) "Cancel source reloads")
            [pressed begin] (geometry/press (geometry/initial-state) image x y)]
        (apply-effects! migrated begin)
        (check! (not (q/read-property migrated "canUndo")) "Cancel preview has no history")
        (let [[cancelled effects] (geometry/cancel pressed)]
          (apply-effects! migrated effects)
          (check! (= [:clear-preview] (mapv :op effects)) "Cancel clears preview once")
          (check! (empty? (second (geometry/cancel cancelled))) "Repeated cancel is inert")))
      (reset! complete? true)
      (qt/quit!)
      (finally (widget/dispose! reference)))))

(defn -main [& _]
  (try
    (qt/run-application!
     #(do (app/start! :image (System/getenv "SNITT_JOLT_IMAGE"))
          (qt/later! 300 #(exercise! (System/getenv "SNITT_JOLT_IMAGE")
                                     (System/getenv "SNITT_JOLT_DATA_DIR"))))
     :app-id "snitt-jolt-gesture-test" :keep-alive true :auto-quit-ms 10000)
    (check! @complete? "Gesture parity completed")
    (println "PASS:" @checks "Jolt and reference gesture checks")
    (finally (settings/clear!) (reset! app/runtime nil) (shutdown-agents))))
