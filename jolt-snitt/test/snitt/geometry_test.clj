(ns snitt.geometry-test
  (:require [clojure.test :refer [deftest is run-tests]]
            [snitt.geometry :as g]))

(defn- geometry [zoom]
  (g/image-geometry {:width (+ 32 (* 80 zoom))
                     :height (+ 32 (* 60 zoom))
                     :image-width 80 :image-height 60}))

(defn- trace [tool zoom start end]
  (let [image (geometry zoom)
        selected (first (g/select-tool (g/initial-state) tool))
        [pressed begin-effects] (apply g/press selected image (g/view-point image start))
        [moved move-effects] (apply g/drag pressed image (g/view-point image end))
        [released end-effects] (apply g/release moved image (g/view-point image end))]
    {:state released :begin begin-effects :move move-effects :end end-effects}))

(deftest viewport-and-source-pixels
  (is (= {:x 16.0 :y 16.0 :width 240.0 :height 180.0 :scale 3.0
          :image-width 80 :image-height 60}
         (geometry 3)))
  (doseq [zoom [0.5 1 3 5]]
    (let [image (geometry zoom)]
      (is (= [19.25 42.5] (g/image-point image (first (g/view-point image [19.25 42.5]))
                                            (second (g/view-point image [19.25 42.5])))))
      (is (= [0.0 60] (g/image-point image -10000 10000)))
      (is (g/contains-point? image (+ (:x image) (:width image))
                                  (+ (:y image) (:height image))))))
  (is (= 0.001 (:scale (g/image-geometry {:width 0 :height 0
                                           :image-width 80 :image-height 60})))))

(deftest reference-gesture-commands
  (doseq [zoom [0.5 1 3 5]]
    (let [{:keys [state begin move end]} (trace "rect" zoom [10.25 11.5] [50.75 45.25])]
      (is (nil? (:gesture state)))
      (is (= [:preview] (mapv :op begin)))
      (is (= [:preview] (mapv :op move)))
      (is (= [:clear-preview :draw] (mapv :op end)))
      (is (= {:op :draw :kind :annotation :tool "rect"
              :start [10.25 11.5] :end [50.75 45.25] :stroke-width 4}
             (last end)))))
  (is (= {:op :draw :kind :cut :vertical? true :from 10 :to 31}
         (last (:end (trace "cut" 3 [10.4 20.0] [30.5 40.1])))))
  (is (= {:op :draw :kind :cut :vertical? false :from 20 :to 51}
         (last (:end (trace "cut" 3 [10.4 20.0] [30.5 50.5])))))
  (is (= {:op :draw :kind :erase :start [6.0 7.0] :end [0.0 60]}
         (let [image (geometry 3)
               [pressed _] (apply g/press (first (g/select-tool (g/initial-state) "erase"))
                                  image (g/view-point image [6 7]))]
           (last (second (g/release pressed image -10000 10000)))))))

(deftest wheel-limits-and-preview-lifecycle
  (let [state (g/image-changed (g/initial-state) 80 60)
        [one effects] (g/wheel state 30 80 60 false)
        [two _] (g/wheel one 29 80 60 false)
        [three changed] (g/wheel two 1 80 60 false)]
    (is (empty? effects))
    (is (= 4 (:stroke-width two)))
    (is (= 5 (:stroke-width three)))
    (is (= [{:op :sizes}] changed))
    (is (= 7 (:stroke-width (first (g/wheel three 12000 80 60 false)))))
    (is (= 1 (:stroke-width (first (g/wheel three -12000 80 60 false))))))
  (let [image (geometry 3)
        [pressed _] (apply g/press (g/initial-state) image (g/view-point image [5 5]))
        [cancelled clear] (g/cancel pressed)
        [_ second-clear] (g/cancel cancelled)
        [_ changed] (g/select-tool pressed "arrow")]
    (is (= [{:op :clear-preview}] clear))
    (is (empty? second-clear))
    (is (= [:clear-preview :tool] (mapv :op changed)))
    (is (= [] (second (g/release cancelled image 40 40))))))

(deftest image-dependent-sizes-and-arrangement-threshold
  (is (= {:stroke 64 :text 144 :pixel 64} (g/limits 1200 800)))
  (is (= {:stroke 7 :text 20 :pixel 30} (g/limits 80 60)))
  (is (= {:stroke 1 :text 8 :pixel 4} (g/limits 8 6)))
  (is (= [1 8 4]
         (let [small (g/image-changed
                      (assoc (g/initial-state) :stroke-width 64 :text-size 144
                             :pixel-block-size 64) 8 6)]
           [(:stroke-width small) (:text-size small) (:pixel-block-size small)])))
  (doseq [zoom [0.5 1 3 5]]
    (let [image (geometry zoom)
          distance (/ 4 zoom)]
      (is (not (g/drag-threshold? image [10 10] [(+ 10 (* distance 0.49)) 10])))
      (is (g/drag-threshold? image [10 10] [(+ 10 distance) 10])))))

(deftest arrangement-traces
  (doseq [zoom [1 3 5]]
    (let [image (geometry zoom)
          [pressed select] (apply g/arrangement-press (g/initial-state) image
                                  (concat (g/view-point image [10 10]) [0]))
          [moved preview] (apply g/arrangement-drag pressed image
                                 (concat (g/view-point image [40 30]) [1]))
          [ended effects] (apply g/arrangement-release moved image
                                 (concat (g/view-point image [40 30]) [1]))]
      (is (= [:select-region :arrangement-preview] (mapv :op select)))
      (is (= [{:op :arrangement-preview :drop 1}] preview))
      (is (= [{:op :arrangement-preview :drop -1}
              {:op :move-region :from 0 :to 1}] effects))
      (is (nil? (:arrange-gesture ended)))
      (is (= [] (second (g/cancel ended))))))
  (let [image (geometry 3)
        [pressed _] (apply g/arrangement-press (g/initial-state) image
                           (concat (g/view-point image [10 10]) [0]))
        [_ effects] (apply g/arrangement-release pressed image
                           (concat (g/view-point image [11 10]) [1]))]
    (is (= [{:op :arrangement-preview :drop -1}] effects))))

(defn -main [& _]
  (let [{:keys [fail error]} (run-tests 'snitt.geometry-test)]
    (when (pos? (+ fail error)) (System/exit 1))))
