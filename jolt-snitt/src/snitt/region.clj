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
  (let [{:keys [x y width height]} (area-rect area)]
    [:rectangle {:key id :id id :x x :y y :width width :height height
                 :clip true :color "transparent" :z 2}
     [:image {:x (- x) :y (- y) :width (get screen "width")
              :height (get screen "height") :source (value/url source)
              :fill-mode "stretch" :asynchronous false}]
     [:rectangle {:anchor "fill" :color "transparent"
                  :border-color "#91bff0" :border-width 2 :z 1}]]))

(defn view [picker screen on-action]
  (let [{:keys [mode regions drag notice]} @(:state picker)
        width (get screen "width") height (get screen "height")
        source (get screen "source")
        area (when (and drag (= (get screen "index") (:screen drag)))
               (normalize [(first (:start drag)) (second (:start drag))
                           (- (first (:end drag)) (first (:start drag)))
                           (- (second (:end drag)) (second (:start drag)))] screen))
        [px py pw ph] (when area (pixel-rect area screen))
        compact? (< width 650)
        mode-button (fn [id label target shortcut]
                      [:button {:key id :id id :label (if compact? (subs label 0 1) label)
                                :tooltip (str label " (" (hint shortcut) ")")
                                :checkable true :checked (= mode target)
                                :sensitive (or (not= target :multiple) (not= mode :video))
                                :on-click #(on-action :mode target)}])]
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
     (for [[index region] (map-indexed vector regions)
           :when (= (get screen "index") (:screen region))]
       (let [[x y _ _] (:area region)]
         [:overlay {:key (:token region) :anchor "fill"}
          (selection-image source screen (:area region) (str "picker-region-" (:token region)))
          [:rectangle {:x x :y y :width 28 :height 28 :color "#91bff0" :z 3}
           [:label {:label (str (inc index)) :color "#15283d" :font-size 16}]]]))
     [:overlay {:key "drag-visual" :anchor "fill" :visible (boolean (and area (pos? pw) (pos? ph)))}
      (selection-image source screen (or area [0 0 0 0]) "picker-drag")
      [:rectangle {:x (bound (- (+ (first (or area [0 0 0 0])) (nth (or area [0 0 0 0]) 2)) 110)
                             0 (- width 110))
                   :y (bound (+ (second (or area [0 0 0 0])) (nth (or area [0 0 0 0]) 3) 8)
                             0 (- height 28))
                   :width 110 :height 28 :z 3 :color "#1b1e23" :radius 5}
       [:label {:color "white" :label (str (or pw 0) " × " (or ph 0) " px")}]]]
     [:rectangle {:x 12 :y 24 :width (max 1 (- width 24)) :height 60 :z 5
                  :radius 12 :color "#be1b1e23" :border-color "#414751" :border-width 1}
      [:hbox {:padding 8 :spacing 6}
       (mode-button (str "picker-single-" (get screen "index")) "Region" :single "regionSingle")
       (mode-button (str "picker-multiple-" (get screen "index")) "Multi" :multiple "regionMultiple")
       (mode-button (str "picker-video-" (get screen "index")) "Video" :video "regionVideo")
       [:label {:label (case mode :video "Drag to record." :multiple "Drag to add regions; click one to remove." "Drag to capture.")
                :color "#b9c1cd" :hexpand true :visible (not compact?)}]
       (when (and (= mode :multiple) (seq regions))
         [:label {:label (if compact? (str (count regions))
                             (str (count regions) (if (= 1 (count regions)) " region" " regions")))
                  :color "#b9c1cd"}])
       (when (and (= mode :multiple) (seq regions))
         [:button {:id (str "picker-arrange-" (get screen "index"))
                   :label (if compact? "✓" "Arrange")
                   :tooltip (str "Continue to Arrange (" (hint "regionArrange") ")")
                   :on-click #(on-action :accept)}])
       [:button {:id (str "picker-cancel-" (get screen "index"))
                 :label (if compact? "Esc" "Cancel")
                 :tooltip (str "Cancel selection (" (hint "regionCancel") ")")
                 :on-click #(on-action :cancel)}]]]
     (when (seq notice)
       [:rectangle {:x 12 :y 92 :width (max 1 (- width 24)) :height 36 :z 5
                    :color "#1b1e23"}
        [:label {:label notice :color "#f1d4a2"}]])
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
                target (if (and (= target :video) (= :video (:mode @(:state picker)))) :single target)
                removed (atom [])]
            (swap! (:state picker) (fn [state]
                                     (let [[next tokens] (change-mode state target)]
                                       (reset! removed tokens) next)))
            (doseq [token @removed] (native! picker "pickerRemove(int)" [token]))
            (native! picker "pickerSetMode(bool)" [(= target :video)]))
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
                                        :title "Snitt — Select a region"
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
