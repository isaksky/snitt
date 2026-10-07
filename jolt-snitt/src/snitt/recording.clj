(ns snitt.recording
  "One Jolt owner for recording policy, overlays and generation-scoped effects."
  (:require [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]))

(def busy-phases #{:preparing :ready :recording :finishing})
(defn busy? [owner] (and owner (contains? busy-phases (:phase @(:state owner)))))
(defn initial [generation]
  {:generation generation :phase :preparing :protected false :started false
   :stop-requested false :canceling false :elapsed 0})

(defn transition [state generation event data]
  (let [{:keys [phase protected started stop-requested canceling]} state]
    (if (or (not= generation (:generation state)) (not (busy-phases phase)))
      [state []]
      (case event
        :protected (if (and (= phase :preparing) (not started))
                     [(assoc state :protected true :started true) [:start]] [state []])
        :ready (if (and (= phase :preparing) protected started)
                 (if stop-requested [(assoc state :phase :finishing) [:finish]]
                     [(assoc state :phase :ready) [:ready-delay]]) [state []])
        :recording (if (= phase :ready) [(assoc state :phase :recording) []] [state []])
        :stop (cond
                (= phase :preparing) [(assoc state :stop-requested true) []]
                (#{:ready :recording} phase) [(assoc state :phase :finishing) [:finish]]
                :else [state []])
        :cancel (if (= phase :finishing) [state []]
                    [(assoc state :phase :finishing :canceling true) [:cancel]])
        :finishing [(assoc state :phase :finishing) []]
        :elapsed [(assoc state :elapsed (try (Long/parseLong data) (catch Throwable _ 0))) []]
        :saved (if (and started (not canceling))
                 [(assoc state :phase :saved :path data) [:terminal]] [state []])
        :canceled [(assoc state :phase :canceled) [:terminal]]
        :failed [(assoc state :phase :failed :error data) [:terminal]]
        [state []]))))

(defn presentation [{:keys [phase elapsed canceling protected]}]
  {:label (case phase :preparing "Starting…" :ready "Ready"
                :recording (str "Recording · " (quot elapsed 60) ":"
                                (when (< (mod elapsed 60) 10) "0") (mod elapsed 60))
                :finishing (if canceling "Canceling…" "Saving…")
                :saved "Saved" :canceled "Canceled" :failed "Failed")
   :stop-enabled (contains? #{:ready :recording} phase)
   :cancel-enabled (contains? #{:preparing :ready :recording} phase)
   :visible (contains? busy-phases phase)
   :indicator-visible (contains? #{:preparing :ready} phase)
   :opacity (if protected 1.0 0.0)})

(declare dispatch!)
(defn controls [owner]
  (let [{:keys [label stop-enabled cancel-enabled]} (presentation @(:state owner))]
    [:rectangle {:hexpand true :vexpand true :color "#ee1b1e23" :radius 12
                 :border-color "#6994bf" :border-width 1}
     [:hbox {:id "recording-controls" :padding 8 :spacing 8}
     [:label {:id "recording-label" :label label :hexpand true}]
     [:button {:id "recording-stop" :label "Stop" :sensitive stop-enabled
               :on-click #(dispatch! owner :stop)}]
     [:button {:id "recording-cancel" :label "Cancel" :sensitive cancel-enabled
               :on-click #(dispatch! owner :cancel)}]]]))

(defn close-windows! [owner]
  (doseq [window (vals @(:windows owner))]
    (when (qt/window-alive? window) (qt/close-window! window)))
  (reset! (:windows owner) {}))

(defn- invoke! [owner signature args] (object/invoke! (:root owner) signature args))

(defn- sync-windows! [owner]
  (let [{:keys [visible indicator-visible opacity]} (presentation @(:state owner))]
    (doseq [[kind window] @(:windows owner)]
      (when (qt/window-alive? window)
        (q/set-property! window "opacity" opacity)
        (q/set-property! window "visible" (and visible (or (not= kind :indicator) indicator-visible)))))))

(defn event! [owner generation event data]
  (when-not @(:closed owner)
    (let [[state effects] (transition @(:state owner) generation event data)]
      (reset! (:state owner) state)
      (sync-windows! owner)
      (doseq [effect effects]
        (case effect
          :start (when-not (invoke! owner "startRecorder(int)" [generation])
                   (event! owner generation :failed "Could not start the protected recorder."))
          :finish (invoke! owner "finishRecorder(int)" [generation])
          :cancel (invoke! owner "cancelRecorder(int)" [generation])
          :ready-delay (qt/later! (:root owner) 180 #(event! owner generation :recording ""))
          :terminal (do
                      (close-windows! owner)
                      (invoke! owner "retireRecorder(int)" [generation])
                      ((:on-terminal owner) state))
          nil)))))

(defn dispatch! [owner action]
  (when owner (event! owner (:generation @(:state owner)) action "")))

(defn owner [root backend generation on-terminal]
  {:root root :backend backend :state (r/atom (initial generation))
   :windows (atom {}) :closed (atom false) :on-terminal on-terminal})

(defn- rect [backend property fallback]
  (let [result (q/read-property backend property)
        area (if (map? result) (:value result) result)]
    (if (and (vector? area) (pos? (nth area 2)) (pos? (nth area 3))) area fallback)))

(defn prepare! [owner]
  (try
    (let [backend (:backend owner)
          region (rect backend "recordingRegion" [20 100 300 200])
          indicator (rect backend "recordingIndicatorGeometry" [80 160 180 180])
          controls (rect backend "recordingControlsGeometry" [20 20 340 58])
          screen (or (first (filter (fn [{[x y w h] :geometry}]
                                     (and (<= x (first region) (+ x w -1))
                                          (<= y (second region) (+ y h -1)))) (qt/screens)))
                     (first (qt/screens)))
          window! (fn [kind [x y width height] view transparent]
                    (let [window (qt/open-window! view :title (str "Snitt — " (name kind))
                                                  :x x :y y :width width :height height :screen screen
                                                  :tool true :frameless true :topmost true
                                                  :input-transparent transparent :transparent-background true
                                                  :opacity 0.0 :visible false
                                                  :on-close (fn [_] (dispatch! owner :cancel) false))]
                      (swap! (:windows owner) assoc kind window)
                      window))
          [x y w h] region
          outline (window! :outline [(- x 3) (- y 3) (+ w 6) (+ h 6)]
                           (fn [] [:overlay {:hexpand true :vexpand true}
                                   [:rectangle {:anchor "fill" :color "transparent"
                                                :border-color "#ef4444" :border-width 3}]]) true)
          indicator (window! :indicator indicator
                             (fn [] [:rectangle {:hexpand true :vexpand true :color "#d91b1e23" :radius 90}
                                     [:vbox {}
                                      [:label {:label (if (= :ready (:phase @(:state owner))) "Ready" "Preparing")
                                              :color "#f0f3f7" :font-size 22 :hexpand true
                                              :text-alignment (value/enum "AlignHCenter")}]]]) true)
          controls (window! :controls controls #(snitt.recording/controls owner) false)
          generation (:generation @(:state owner))]
      (if (invoke! owner "prepareRecorder(int,QObject*,QObject*,QObject*)"
                   [generation (value/object indicator) (value/object controls) (value/object outline)])
        (event! owner generation :protected "")
        (event! owner generation :failed "Could not exclude Snitt's recording controls from capture. Recording was canceled.")))
    (catch Throwable error
      (event! owner (:generation @(:state owner)) :failed
              (str "Could not prepare recording: " (.getMessage error))))))

(defn dispose! [owner]
  (when (and owner (not @(:closed owner)))
    (reset! (:closed owner) true)
    (close-windows! owner)
    (when (q/alive? (:root owner))
      (invoke! owner "retireRecorder(int)" [(:generation @(:state owner))]))))
