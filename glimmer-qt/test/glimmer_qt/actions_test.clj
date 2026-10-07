(ns glimmer-qt.actions-test
  (:require [glimmer.core :as ui] [glimmer.ratom :as r]
            [glimmer-qt.core :as qt] [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q] [glimmer-qt.value :as v] [jolt.ffi :as ffi]))
(ffi/defcfn click "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn input "gqt_test_action_input" [:uint64 :int] :int :blocking)
(ffi/defcfn space "gqt_test_space" [:uint64] :int :blocking)
(def state (r/atom {:checked false :enabled true :shortcut "Space" :generation 0}))
(def clicks (atom []))
(def checks (atom 0))
(def done (atom false))
(defn check [v message] (swap! checks inc) (when-not v (throw (ex-info message {}))))
(defn id [s] (w/find-by-id s))
(defn p [s key] (q/read-property (id s) key))
(defn source [path] (str "file://" (System/getProperty "user.dir") "/" path))
(defn root []
  (let [{:keys [checked enabled shortcut generation]} @state]
    [:vbox {:hexpand true :vexpand true :padding 20 :spacing 12}
     [:label {:label "Toolbar actions · focus, state and typography" :font-size 22 :font-weight 600}]
     [:hbox {:spacing 12}
      [:button {:id "tool" :icon-only true :label "Play" :accessible-name "Play recording"
                :accessible-description "Toggle playback" :tooltip (str "Play (" shortcut ")")
                :icon (source "../src/icons/play.svg") :checkable true :checked checked
                :sensitive enabled :on-click #(swap! clicks conj generation)}]
      [:button {:id "good" :label "Keep" :mode "good" :checked true :checkable true}]
      [:button {:id "bad" :label "Discard" :mode "bad" :checked true :checkable true}]
      [:button {:id "primary" :label "Save capture" :highlighted true :mode "good"}]
      [:button {:id "disabled" :label "Unavailable" :sensitive false :on-click #(swap! clicks conj :disabled)}]]
     [:label {:id "counter" :label "00:08 / 12:34" :font-family "Menlo" :font-size 18}]
     [:label {:id "wrapped" :label "A readable toolbar label wraps when its available width is constrained."
              :hexpand true :max-width 280 :wrap (v/enum "WordWrap")}]
     [:label {:id "elided" :label "An intentionally long single-line label is elided at the right edge"
              :hexpand true :max-width 280 :wrap (v/enum "NoWrap") :elide (v/enum "ElideRight")
              :text-alignment (v/enum "AlignRight")}]
     [:button {:id "custom" :label "Custom content" :checked checked :checkable true
               :content-source (source "test/qml/ActionContent.qml")
               :background-source (source "test/qml/ActionBackground.qml")} ]
     [:button {:id "light" :label "Light theme · violet accent" :theme (v/enum "Light")
               :accent "#b28ce6" :highlighted true}]]))
(defn steps [xs] (when-let [f (first xs)] (qt/later! 150 #(do (f) (steps (rest xs))))))
(defn run-checks [window path]
  (let [tool (id "tool")]
    (check (try (w/create! :button {:icon-only true}) false (catch Throwable _ true))
           "Icon-only control requires an accessible name")
    (check (try (w/create! :button {:mode "unknown"}) false (catch Throwable _ true))
           "Unknown action modes rejected")
    (check (= (v/enum "AlignLeft") (p "wrapped" "horizontalAlignment")) "Text alignment")
    (check (= (v/enum "ElideRight") (p "elided" "elide")) "Text elision")
    (check (= "Custom content" (q/read-property (q/child-target (id "custom") "customContentItem") "text")) "Custom content loaded")
    (check (not= 0 (q/child-target (id "custom") "customBackgroundItem")) "Custom background loaded")
    (check (= "Play recording" (p "tool" "Accessible.name")) "Icon accessible name")
    (check (= "Toggle playback" (p "tool" "Accessible.description")) "Accessible description")
    (let [a (q/measure-text (id "counter") "00:00") b (q/measure-text (id "counter") "88:88")]
      (check (= (get a "advance") (get b "advance")) "Stable-width counters")
      (check (pos? (get a "ascent")) "Baseline metrics")
      (check (pos? (get a "line-width")) "Underline thickness"))
    (check (pos? (get (q/measure-text tool "P") "advance")) "Shortcut letter measurement")
    (swap! state assoc :checked true :enabled false :shortcut "K" :generation 1)
    (steps
     [(fn []
        (check (= "Custom content ✓" (q/read-property (q/child-target (id "custom") "customContentItem") "text")) "Custom content reacts to control state")
        (check (= tool (id "tool")) "Reactive identity")
        (check (p "tool" "checked") "Checked prop")
        (check (not (p "tool" "enabled")) "Disabled prop")
        (check (= "Play (K)" (p "tool" "ToolTip.text")) "Live shortcut tooltip")
        (check (empty? @clicks) "State changes never click")
        (click tool) (click (id "disabled"))
        (check (empty? @clicks) "Disabled hit testing consumes no clicks")
        (swap! state assoc :enabled true))
      (fn [] (click tool) (check (= [1] @clicks) "Fresh handler called once")
        (swap! state assoc :generation 2 :checked true))
      (fn []
        (check (p "tool" "checked") "Controlled checked restored after native toggle")
        (click tool) (check (= [1 2] @clicks) "Handler replacement has no duplicate connections")
        (input tool 0))
      (fn [] (check (p "tool" "hovered") "Real pointer hover") (input tool 1))
      (fn [] (check (p "tool" "down") "Real pointer pressed state") (input tool 2)
        (q/invoke! (id "light") "forceActiveFocus()" [])
        (input tool 3))
      (fn []
        (check (p "tool" "visualFocus") "Keyboard focus indication")
        (space window)
        (check (= [1 2 2 2] @clicks) "Space activates focused tool once")
        (q/set-property! tool "ToolTip.visible" false)
        (when path (q/save-window @w/session path))
        (input tool 3))
      (fn [] (check (p "good" "visualFocus") "Tab traverses real controls")
        (w/apply-props! :button (id "custom") {:id "custom" :label "Restored"})
        (check (= "" (p "custom" "contentSource")) "Removed content source resets")
        (check (= "" (p "custom" "backgroundSource")) "Removed background source resets")
        (check (not (p "custom" "checked")) "Removed checked resets")
        (reset! done true) (qt/quit!))])))
(defn -main [& [path]]
  (try
    (qt/run-application!
     #(let [window (qt/open-window! root :width 700 :height 460 :title "Styled action verification")]
        (qt/later! 250 (fn [] (run-checks window path)))) :auto-quit-ms 8000)
    (check @done "Checks completed")
    (check (zero? (w/live-count)) "Controls disposed")
    (println "PASS:" @checks "action checks")
    (finally (shutdown-agents))))
