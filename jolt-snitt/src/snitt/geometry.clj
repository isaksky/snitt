(ns snitt.geometry
  "Pure image-space geometry and annotation gesture decisions.")

(defn image-geometry [{:keys [width height image-width image-height]}]
  (when (and (pos? image-width) (pos? image-height))
    (let [scale (max 0.001 (min (/ (- width 32.0) image-width)
                                (/ (- height 32.0) image-height)))
          w (* image-width scale)
          h (* image-height scale)]
      {:x (/ (- width w) 2.0) :y (/ (- height h) 2.0)
       :width w :height h :scale scale
       :image-width image-width :image-height image-height})))

(defn contains-point? [{:keys [x y width height]} px py]
  (and (<= x px (+ x width)) (<= y py (+ y height))))

(defn image-point [{:keys [x y scale image-width image-height]} px py]
  [(max 0.0 (min image-width (/ (- px x) scale)))
   (max 0.0 (min image-height (/ (- py y) scale)))])

(defn view-point [{:keys [x y scale]} [px py]]
  [(+ x (* px scale)) (+ y (* py scale))])

(defn limits [image-width image-height]
  (let [edge (min image-width image-height)
        image? (and (pos? image-width) (pos? image-height))]
    {:stroke (if image? (max 1 (min 64 (quot edge 8))) 64)
     :text (if image? (max 8 (min 144 (quot edge 3))) 144)
     :pixel (if image? (max 4 (min 64 (quot edge 2))) 64)}))

(defn initial-state []
  {:tool "rect" :stroke-width 4 :text-size 24 :pixel-block-size 12
   :wheel-remainder 0.0 :gesture nil})

(defn- clamp [low value high] (max low (min high value)))

(defn image-changed [state image-width image-height]
  (let [{:keys [stroke text pixel]} (limits image-width image-height)]
    (-> state
        (assoc :gesture nil :arrange-gesture nil)
        (update :stroke-width #(clamp 1 % stroke))
        (update :text-size #(clamp 8 % text))
        (update :pixel-block-size #(clamp 4 % pixel)))))

(defn cancel [state]
  (cond
    (:gesture state) [(assoc state :gesture nil) [{:op :clear-preview}]]
    (:arrange-gesture state) [(assoc state :arrange-gesture nil)
                              [{:op :arrangement-preview :drop -1}]]
    :else [state []]))

(defn select-tool [state tool]
  (if (= tool (:tool state)) [state []]
    (let [[next effects] (cancel state)]
      [(assoc next :tool tool :wheel-remainder 0.0)
       (conj effects {:op :tool :tool tool})])))

(defn- preview-command [{:keys [tool start end] :as gesture}]
  (let [[sx sy] start [ex ey] end]
    {:op :preview :gesture gesture
     :vertical? (>= (abs (- ex sx)) (abs (- ey sy)))}))

(defn wheel [state delta image-width image-height arranging?]
  (if (or arranging? (not (pos? image-width)) (not (pos? image-height))
          (not (#{"rect" "arrow" "text" "blur"} (:tool state))))
    [state []]
    (let [remainder (+ (:wheel-remainder state) (* delta 2.0))
          steps (int (/ remainder 120.0))
          next (assoc state :wheel-remainder (- remainder (* steps 120)))]
      (if (zero? steps) [next []]
        (let [bounds (limits image-width image-height)
              next (case (:tool state)
                     "blur" (update next :pixel-block-size #(clamp 4 (+ % (* steps 2)) (:pixel bounds)))
                     "text" (update next :text-size #(clamp 8 (+ % steps) (:text bounds)))
                     (update next :stroke-width #(clamp 1 (+ % steps) (:stroke bounds))))]
          [next [{:op :sizes}]])))))

(defn press [state geometry x y]
  (if (or (nil? geometry) (not (contains-point? geometry x y))) [state []]
    (let [point (image-point geometry x y)]
      (if (= "text" (:tool state))
        [state [{:op :text-request :point point}]]
        (let [next (assoc state :gesture {:tool (:tool state) :start point :end point})]
          [next [(preview-command (:gesture next))]])))))

(defn drag [state geometry x y]
  (if-let [gesture (:gesture state)]
    (let [next (assoc state :gesture (assoc gesture :end (image-point geometry x y)))]
      [next [(preview-command (:gesture next))]])
    [state []]))

(defn- qround [value] (int (Math/floor (+ value 0.5))))

(defn draw-command [{:keys [tool start end]} state]
  (let [[sx sy] start [ex ey] end]
    (case tool
      "cut" (let [vertical? (>= (abs (- ex sx)) (abs (- ey sy)))]
                {:op :draw :kind :cut :vertical? vertical?
                 :from (qround (if vertical? sx sy))
                 :to (qround (if vertical? ex ey))})
      "blur" {:op :draw :kind :blur :start start :end end
              :block-size (:pixel-block-size state)}
      "erase" {:op :draw :kind :erase :start start :end end}
      {:op :draw :kind :annotation :tool tool :start start :end end
       :stroke-width (:stroke-width state)})))

(defn release [state geometry x y]
  (if (:gesture state)
    (let [[next _] (drag state geometry x y)
          gesture (:gesture next)]
      [(assoc next :gesture nil)
       [{:op :clear-preview} (draw-command gesture next)]])
    [state []]))

(defn drag-threshold? [geometry [sx sy] [ex ey]]
  (>= (* (+ (abs (- ex sx)) (abs (- ey sy))) (:scale geometry)) 4.0))

(defn arrangement-press [state geometry x y index]
  (if (or (nil? geometry) (not (contains-point? geometry x y)) (neg? index))
    [state []]
    (let [point (image-point geometry x y)]
      [(assoc state :arrange-gesture {:start point :end point :from index :drop index})
       [{:op :select-region :index index}
        {:op :arrangement-preview :drop index}]])))

(defn arrangement-drag [state geometry x y index]
  (if-let [gesture (:arrange-gesture state)]
    [(assoc state :arrange-gesture
            (assoc gesture :end (image-point geometry x y) :drop index))
     [{:op :arrangement-preview :drop index}]]
    [state []]))

(defn arrangement-release [state geometry x y index]
  (if (:arrange-gesture state)
    (let [[next _] (arrangement-drag state geometry x y index)
          {:keys [start end from drop]} (:arrange-gesture next)]
      [(assoc next :arrange-gesture nil)
       (cond-> [{:op :arrangement-preview :drop -1}]
         (drag-threshold? geometry start end)
         (conj {:op :move-region :from from :to drop}))])
    [state []]))
