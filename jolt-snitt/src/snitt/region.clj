(ns snitt.region
  "Display-local picker geometry and ordered selection policy. Pixels stay native."
  (:require [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [snitt.settings :as settings]))

(defn initial [multiple video]
  {:mode (cond video :video multiple :multiple :else :single)
   :regions [] :drag nil :notice ""})

(defn- bound [n low high] (max low (min n high)))
(defn- qt-round [n] (long (Math/floor (+ n 0.5))))

(defn normalize [[x y width height] screen]
  (let [x1 (bound (min x (+ x width)) 0 (get screen "width"))
        x2 (bound (max x (+ x width)) 0 (get screen "width"))
        y1 (bound (min y (+ y height)) 0 (get screen "height"))
        y2 (bound (max y (+ y height)) 0 (get screen "height"))]
    [x1 y1 (- x2 x1) (- y2 y1)]))

(defn pixel-rect [area screen]
  (let [[x y width height] (normalize area screen)
        sx (/ (get screen "pixelWidth") (get screen "width"))
        sy (/ (get screen "pixelHeight") (get screen "height"))
        px (qt-round (* x sx)) py (qt-round (* y sy))]
    [px py (- (qt-round (* (+ x width) sx)) px)
     (- (qt-round (* (+ y height) sy)) py)]))

(defn- contains-point? [[x y width height] px py]
  (and (<= x px (+ x width)) (<= y py (+ y height))))

(defn press [state screen event]
  (case (:button event)
    2 [state {:action :cancel}]
    1 [(assoc state :drag {:screen (get screen "index")
                           :start [(:x event) (:y event)]
                           :end [(:x event) (:y event)]}) nil]
    [state nil]))

(defn move [state event]
  (if (:drag state)
    (assoc-in state [:drag :end] [(:x event) (:y event)])
    state))

(defn cancel-drag [state] (assoc state :drag nil))

(defn release [state screen event]
  (if-let [{:keys [start]} (:drag state)]
    (let [end [(:x event) (:y event)]
          area (normalize [(first start) (second start)
                           (- (first end) (first start)) (- (second end) (second start))] screen)
          [px py pw ph] (pixel-rect area screen)
          click? (< (+ (Math/abs (- (first end) (first start)))
                     (Math/abs (- (second end) (second start)))) 4)
          hit (when (and click? (= :multiple (:mode state)))
                (first (filter (fn [region]
                                 (and (= (get screen "index") (:screen region))
                                      (contains-point? (:area region) (:x event) (:y event))))
                               (reverse (:regions state)))))]
      [(assoc state :drag nil)
       (cond
         hit {:action :remove :token (:token hit)}
         (and (>= pw 2) (>= ph 2))
         {:action (case (:mode state) :video :record :multiple :add :select)
          :screen (get screen "index") :area area :pixels [px py pw ph]}
         :else nil)])
    [state nil]))

(defn change-mode [state mode]
  (if (= mode (:mode state)) [state []]
      [(assoc state :mode mode :drag nil :notice ""
              :regions (if (= mode :multiple) (:regions state) []))
       (if (= mode :multiple) [] (mapv :token (:regions state)))]))

(defn add [state screen area token]
  (-> state (update :regions conj {:screen screen :area area :token token})
      (assoc :notice "")))

(defn remove-token [state token]
  (update state :regions (fn [regions] (vec (remove #(= token (:token %)) regions)))))

(defn shortcuts [action]
  (get-in @settings/published [:shortcuts action] []))

(defn hint [action]
  (get-in @settings/published [:hints action] ""))

(defn- area-rect [[x y width height]]
  {:x x :y y :width width :height height})

(defn- selection-image [source screen area id]
  ;; Quick rounds the far scissor edges outward. End beneath the centered
  ;; opaque border so a fractional selection cannot expose an exterior pixel.
  (let [{:keys [x y width height]} (area-rect area)]
    [:overlay {:key id :id id :x x :y y :width (max 0 (- width 1))
               :height (max 0 (- height 1)) :z 2 :clip true}
     [:image {:x (- x) :y (- y) :width (get screen "width")
              :height (get screen "height") :source (value/url source)
              :fill-mode "stretch" :asynchronous false}]]))

(defn view [picker screen on-action]
  (let [{:keys [mode regions drag notice]} @(:state picker)
        width (get screen "width") height (get screen "height")
        source (get screen "source")
        area (when (and drag (= (get screen "index") (:screen drag)))
               (normalize [(first (:start drag)) (second (:start drag))
                           (- (first (:end drag)) (first (:start drag)))
                           (- (second (:end drag)) (second (:start drag)))] screen))
        [px py pw ph] (when area (pixel-rect area screen))
        panel-width (min 960 (max 1 (- width 24)))]
    [:overlay {:id (str "picker-root-" (get screen "index"))
               :hexpand true :vexpand true :clip true}
     [:image {:id (str "picker-background-" (get screen "index")) :anchor "fill"
              :source (value/url source) :fill-mode "stretch" :asynchronous false}]
     [:rectangle {:anchor "fill" :color "#73000000"}]
     [:pointer-area {:id (str "picker-input-" (get screen "index")) :anchor "fill" :z 4
                     :buttons (value/flags "LeftButton" "RightButton")
                     :prevent-stealing true :cursor (value/enum "CrossCursor")
                     :on-pointer-press #(do (on-action :press screen %) true)
                     :on-pointer-move #(on-action :move screen %)
                     :on-pointer-release #(on-action :release screen %)
                     :on-pointer-cancel #(on-action :cancel-drag screen %)}]
     (for [region regions :when (= (get screen "index") (:screen region))]
       (selection-image source screen (:area region) (str "picker-region-" (:token region))))
     (when area (selection-image source screen area "picker-drag"))
     [:selection-decoration {:id (str "picker-decoration-" (get screen "index")) :width width :height height :z 5
                             :regions (mapv (fn [[index region]]
                                              (let [[x y w h] (:area region)]
                                                {"x" x "y" y "width" w "height" h
                                                 "number" (inc index) "token" (:token region)}))
                                            (filter (fn [[_ region]] (= (get screen "index") (:screen region)))
                                                    (map-indexed vector regions)))
                             :dragging (boolean area) :drag-area (apply value/rect-f (or area [0 0 0 0]))
                             :dimensions (str (or pw 0) " × " (or ph 0) " px") :notice notice
                             :on-remove #(on-action :remove (long %))}]
     [:selector-toolbar {:id (str "picker-toolbar-" (get screen "index"))
                         :x (quot (- width panel-width) 2) :y 24 :width panel-width :height 60 :z 6
                         :screen-width width :mode (name mode) :selection-count (count regions)
                         :hints (:hints @settings/published) :shortcuts (:shortcuts @settings/published)
                         :on-action #(on-action (keyword %))}]
     (for [[action id enabled?] [["regionCancel" :cancel true]
                                 ["regionSingle" :single true]
                                 ["regionMultiple" :multiple (not= mode :video)]
                                 ["regionVideo" :video true]
                                 ["regionArrange" :accept (and (= mode :multiple) (seq regions) (nil? drag))]
                                 ["regionRemoveLast" :remove-last (= mode :multiple)]]]
       [:shortcut {:key action :id (str "picker-shortcut-" action)
                   :sequences (shortcuts action) :enabled (boolean enabled?)
                   :on-activate #(on-action id)}])]))

(defn close! [picker]
  (when picker
    (doseq [window @(:windows picker)]
      (when (qt/window-alive? window) (qt/close-window! window)))
    (reset! (:windows picker) [])))

(defn- native! [picker signature args]
  (object/invoke! (:backend picker) signature args))

(defn- run-effect! [picker effect]
  (when effect
    (let [{:keys [action screen area token]} effect]
      (case action
        :cancel (native! picker "pickerCancel()" [])
        :select (native! picker "pickerSingle(int,QRectF)"
                         [screen (apply value/rect-f area)])
        :record (native! picker "pickerVideo(int,QRectF)"
                         [screen (apply value/rect-f area)])
        :add (let [new-token (native! picker "pickerAdd(int,QRectF)"
                                      [screen (apply value/rect-f area)])]
               (when (>= new-token 0)
                 (swap! (:state picker) add screen area new-token)))
        :remove (do (native! picker "pickerRemove(int)" [token])
                    (swap! (:state picker) remove-token token))
        nil))))

(defn- dispatch! [picker action & args]
  (case action
    :press (let [[screen event] args
                 effect (atom nil)]
             (swap! (:state picker) (fn [state]
                                      (let [[next effect-value] (press state screen event)]
                                        (reset! effect effect-value) next)))
             (run-effect! picker @effect))
    :move (swap! (:state picker) move (second args))
    :release (let [[screen event] args
                   effect (atom nil)]
               (swap! (:state picker) (fn [state]
                                        (let [[next effect-value] (release state screen event)]
                                          (reset! effect effect-value) next)))
               (run-effect! picker @effect))
    :cancel-drag (swap! (:state picker) cancel-drag)
    :cancel (run-effect! picker {:action :cancel})
    :mode (let [target (first args)
                target (if (and (= target :multiple) (= :video (:mode @(:state picker)))) :video target)
                removed (atom [])]
            (swap! (:state picker) (fn [state]
                                     (let [[next tokens] (change-mode state target)]
                                       (reset! removed tokens) next)))
            (doseq [token @removed] (native! picker "pickerRemove(int)" [token]))
            (native! picker "pickerSetMode(bool)" [(= target :video)])
            (doseq [window @(:windows picker) :when (qt/window-alive? window)]
              (q/set-property! window "title" (if (= target :video)
                                                "Snitt — Select a recording region" "Snitt — Select a region"))))
    :single (dispatch! picker :mode :single)
    :multiple (dispatch! picker :mode :multiple)
    :video (dispatch! picker :mode :video)
    :remove (run-effect! picker {:action :remove :token (first args)})
    :remove-last (when-let [region (last (:regions @(:state picker)))]
                   (run-effect! picker {:action :remove :token (:token region)}))
    :accept (when (and (= :multiple (:mode @(:state picker)))
                       (seq (:regions @(:state picker))))
              (native! picker "pickerAccept(QStringList)"
                       [(mapv (comp str :token) (:regions @(:state picker)))]))
    nil))

(defn open! [backend screens multiple video]
  (let [picker {:backend backend :state (r/atom (initial multiple video))
                :windows (atom [])}
        available (qt/screens)]
    (try
      (doseq [screen screens]
        (let [placement (first (filter #(= (:geometry %)
                                          [(get screen "x") (get screen "y")
                                           (get screen "width") (get screen "height")])
                                       available))]
          (when-not placement
            (throw (ex-info "Selected display changed during capture" {:screen screen})))
          (let [window (qt/open-window! #(view picker screen (fn [& action] (apply dispatch! picker action)))
                                        :title (if video "Snitt — Select a recording region" "Snitt — Select a region")
                                        :width (get screen "width") :height (get screen "height")
                                        :screen placement :x (get screen "x") :y (get screen "y")
                                        :tool true :frameless true :topmost true
                                        :on-close (fn [_] (dispatch! picker :cancel) false))]
            (swap! (:windows picker) conj window))))
      (when-let [window (first @(:windows picker))] (qt/activate! window))
      picker
      (catch Throwable error
        (close! picker)
        (throw error)))))
