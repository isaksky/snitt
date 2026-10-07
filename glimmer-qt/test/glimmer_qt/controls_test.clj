(ns glimmer-qt.controls-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q]
            [jolt.ffi :as ffi]))

(ffi/defcfn native-input "gqt_test_slider_input" [:uint64 :int :double] :int :blocking)
(ffi/defcfn key! "gqt_test_key" [:uint64 :int :int] :int :blocking)
(defn input! [slider action fraction]
  (native-input slider action (double fraction)))
(def events (atom []))
(def checks (atom 0))
(def done (atom false))
(defn check [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {:events @events}))))
(defn id [name] (w/find-by-id name))
(defn p [name property] (q/read-property (id name) property))
(defn near? [a b] (< (abs (- a b)) 0.03))
(defn slider-props [value generation]
  {:id "seek" :hexpand true :preferred-height 32
   :from 1.5 :to 2.5 :value value :step 0
   :accessible-name "Playback position"
   :on-change #(swap! events conj [:move generation %])
   :on-interacting-change #(swap! events conj [:interacting generation %])})
(defn root []
  [:vbox {:hexpand true :vexpand true :padding 24 :spacing 16}
   [:slider (slider-props 2.0 :old)]
   [:progress {:id "export" :hexpand true :from 0 :to 100 :value 25
               :accessible-name "Export progress"}]
   [:progress {:id "prepare" :hexpand true :indeterminate true
               :accessible-name "Preparing export"}]
   [:busy-indicator {:id "busy" :running true :accessible-name "Working"}]])

(defn -main [& [screenshot]]
  (try
    (qt/run-application!
     #(let [window (qt/open-window! root :width 480 :height 260
                                    :title "Slider and progress verification")]
        (qt/later! 300
          (fn []
            (let [slider (id "seek")]
              (check (= "Playback position" (p "seek" "Accessible.name")) "Slider accessible name")
              (check (near? 2.0 (p "seek" "value")) "Initial controlled value")
              (check (near? 0.25 (p "export" "position")) "Determinate progress position")
              (check (p "prepare" "indeterminate") "Preparation progress is indeterminate")
              (check (p "busy" "running") "Busy indicator runs")
              (when screenshot (q/save-window @w/session screenshot))
              (w/apply-props! :slider slider (slider-props 2.25 :new))
              (check (near? 2.25 (p "seek" "value")) "Programmatic playhead update")
              (check (empty? @events) "Programmatic updates emit no user events")
              (input! slider 0 0.25)
              (check (some #{[:interacting :new true]} @events) "Press reports interaction")
              (check (p "seek" "pressed") "Native pressed state is exposed")
              (input! slider 1 0.75)
              (check (some #(and (= :move (first %)) (= :new (second %))
                                 (near? 2.25 (nth % 2))) @events)
                     "Real drag reports user movement")
              (input! slider 2 0.75)
              (check (= [:interacting :new false] (last @events)) "Release ends interaction")
              (check (not (p "seek" "pressed")) "Released native state")
              (reset! events [])
              (w/apply-props! :slider slider (slider-props 2.0 :fresh))
              (check (near? 2.0 (p "seek" "value")) "Controlled value restored after drag")
              (check (empty? @events) "Controlled restoration does not feed back")
              (q/invoke! slider "forceActiveFocus()" [])
              (key! window 16777236 0) ; Qt::Key_Right
              (check (some #(= [:move :fresh] (subvec % 0 2)) @events)
                     "Keyboard adjustment uses fresh handler")
              (check (> (p "seek" "value") 2.0) "Keyboard adjustment changes value")
              (reset! events [])
              (w/apply-props! :slider slider (assoc (slider-props 2.0 :disabled) :sensitive false))
              (input! slider 0 0.75) (input! slider 2 0.75)
              (key! window 16777236 0)
              (check (empty? @events) "Disabled slider ignores pointer and keyboard")
              (w/apply-props! :slider slider (assoc (slider-props 9.0 :clamp)
                                                   :from 0 :to 0.2 :step 1 :sensitive true))
              (check (near? 0.2 (p "seek" "value")) "Value clamps to short range")
              (input! slider 0 0.0) (input! slider 1 1.0) (input! slider 2 1.0)
              (check (<= 0 (p "seek" "value") 0.2) "Large step stays in short range")
              (q/invoke! slider "forceActiveFocus()" [])
              (key! window 16777234 0) ; Qt::Key_Left
              (check (near? 0 (p "seek" "value")) "Large keyboard step clamps to lower bound")
              (w/apply-props! :slider slider (assoc (slider-props 5 :zero)
                                                   :from 5 :to 5 :value 5 :step 0))
              (reset! events [])
              (input! slider 0 0.8) (input! slider 2 0.8)
              (check (near? 5 (p "seek" "value")) "Zero range remains fixed")
              (check (not-any? #(= :move (first %)) @events) "Zero range has no user value change")
              (check (try (w/apply-props! :slider slider
                            (assoc (slider-props 0 :invalid) :from 2 :to 1)) false
                          (catch Throwable _ true)) "Reversed ranges are rejected")
              (w/apply-props! :progress (id "export")
                              {:id "export" :hexpand true :from 0 :to 100 :value 80})
              (check (near? 0.8 (p "export" "position")) "Determinate progress updates")
              (w/apply-props! :progress (id "prepare")
                              {:id "prepare" :hexpand true :from 0 :to 1 :value 0.5})
              (check (not (p "prepare" "indeterminate")) "Preparation switches to determinate")
              (check (near? 0.5 (p "prepare" "position")) "Switched progress value")
              (w/apply-props! :busy-indicator (id "busy") {:id "busy" :running false})
              (check (not (p "busy" "running")) "Busy animation stops")
              (reset! done true)
              (qt/quit!)))))
     :auto-quit-ms 8000)
    (check @done "Control checks completed")
    (check (zero? (w/live-count)) "Controls disposed")
    (println "PASS:" @checks "slider and progress checks")
    (finally (shutdown-agents))))
