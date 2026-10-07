(ns glimmer-qt.keyboard-test
  (:require [glimmer-qt.core :as qt] [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q] [jolt.ffi :as ffi]))
(ffi/defcfn key! "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn ime! "gqt_test_ime" [:uint64 :int] :int :blocking)
(ffi/defcfn modal! "gqt_test_modal" [:uint64 :int] :int :blocking)
(def other (atom nil))
(def checks (atom 0))
(def done (atom false))
(def hits (atom []))
(def releases (atom []))
(def events (atom []))
(defn check [v message] (swap! checks inc) (when-not v (throw (ex-info message {}))))
(defn id [s] (w/find-by-id s))
(defn root []
  [:vbox {:hexpand true :vexpand true}
   [:item {:id "canvas" :tab-focus true :preferred-height 80 :hexpand true}]
   [:entry {:id "edit" :text ""}]
   [:button {:id "next" :label "Next"}]
   [:shortcut {:id "command" :sequences ["Ctrl+K" "Alt+K"] :on-activate #(swap! hits conj :old)}]])
(defn steps [xs] (when-let [f (first xs)] (qt/later! 100 #(do (f) (steps (rest xs))))))
(defn -main [& _]
  (try
    (qt/run-application!
     #(let [window (qt/open-window! root :title "Keyboard verification")]
        (qt/later! 300
          (fn []
            (qt/activate! window)
            (w/focus! (id "canvas"))
            (steps
             [(fn []
                (check (q/read-property (id "canvas") "activeFocus") "Canvas focus")
                (key! window 75 67108864)
                (key! window 75 134217728)
                (check (= [:old :old] @hits) "Command/Control and alternate Alt sequence")
                (key! window 75 268435456)
                (check (= 2 (count @hits)) "Meta/physical Control distinct from Command on macOS")
                (w/apply-props! :shortcut (id "command")
                  {:id "command" :sequences ["Ctrl+L" "Ctrl+Shift+L"] :on-activate #(swap! hits conj :new)}))
              (fn []
                (key! window 75 67108864)
                (key! window 76 67108864)
                (key! window 76 100663296)
                (check (= [:old :old :new :new] @hits) "Live sequence and handler replacement exactly once")
                (w/apply-props! :shortcut (id "command") {:id "command" :sequences ["Ctrl+L"] :enabled false})
                (key! window 76 67108864)
                (check (= 4 (count @hits)) "Disabled shortcut group")
                (key! window 16777217 0))
              (fn []
                (check (q/read-property (id "edit") "activeFocus") "Real Tab traverses to entry")
                (w/focus-next! (id "edit"))
                (check (q/read-property (id "next") "activeFocus") "Explicit forward traversal")
                (w/focus-next! (id "next") false)
                (check (q/read-property (id "edit") "activeFocus") "Explicit backward traversal")
                (w/apply-props! :entry (id "edit")
                  {:id "edit" :text "" :on-key-release #(do (swap! releases conj %) false) :on-activate #(swap! hits conj :accepted)
                   :on-key-press (fn [e] (swap! events conj e)
                                   (when (#{16777216 16777220 16777221} (:key e))
                                     (w/focus! (id "canvas")) true))})
                (key! window 16777220 0)
                (check (q/read-property (id "canvas") "activeFocus") "Return commit restores canvas focus")
                (check (= 4 (count @hits)) "Accepted Return suppresses default entry activation")
                (w/focus! (id "edit")) (key! window 16777221 0)
                (check (q/read-property (id "canvas") "activeFocus") "Keypad Enter accepted")
                (w/focus! (id "edit")) (key! window 16777216 0)
                (check (q/read-property (id "canvas") "activeFocus") "Escape cancel restores canvas focus")
                (w/focus! (id "edit")) (key! window 65 0)
                (check (= 65 (:key (last @releases))) "Key release delivered")
                (check (= {:key 65 :text "a" :modifiers 0 :auto-repeat false :count 1}
                          (last @events)) (str "Key payload preserves Qt fields: " (pr-str (last @events))))
                (check (= "a" (q/read-property (id "edit") "text")) "Unaccepted keys retain text editing")
                (let [n (count @events)]
                  (ime! (id "edit") 0) (key! window 16777220 0)
                  (check (= n (count @events)) "Preedit key bypasses command handlers")
                  (ime! (id "edit") 1)
                  (check (.contains (q/read-property (id "edit") "text") "日本") "IME commit preserved"))
                (w/apply-props! :entry (id "edit") {:id "edit" :text ""})
                (let [n (count @events)] (key! window 66 0)
                  (check (= n (count @events)) "Removed key handler disconnected"))
                (w/apply-props! :shortcut (id "command")
                  {:id "command" :sequences ["Ctrl+L"] :on-activate #(swap! hits conj :scoped)})
                (reset! other (qt/open-window! (fn [] [:label {:label "Other window"}])))
                (qt/activate! @other))
              (fn []
                (key! @other 76 67108864)
                (check (not= :scoped (last @hits)) "Window shortcut excluded from other window")
                (w/apply-props! :shortcut (id "command")
                  {:id "command" :scope "application" :sequences ["Ctrl+L"]
                   :on-activate #(swap! hits conj :app)}))
              (fn []
                (key! @other 76 67108864)
                (check (= :app (last @hits)) "Application shortcut works across windows")
                (qt/activate! window))
              (fn []
                (w/apply-props! :shortcut (id "command")
                  {:id "command" :sequences ["Ctrl+L"] :enabled false
                   :on-activate #(swap! hits conj :blocked)})
                (w/focus! (id "edit")) (key! window 76 67108864)
                (check (not= :blocked (last @hits)) "Editing state suppresses editor commands")
                (modal! window 1))
              (fn []
                (let [n (count @hits)] (key! window 76 67108864)
                  (check (= n (count @hits)) "Modal state suppresses editor commands"))
                (modal! window 0) (qt/activate! window))
              (fn []
                (w/focus! (id "canvas"))
                (check (q/read-property (id "canvas") "activeFocus") "Focus restored after modal cancel")
                (let [shortcut (id "command")]
                  (w/dispose! shortcut)
                  (check (not (q/alive? shortcut)) "Shortcut ownership disposed")
                  (key! window 76 67108864)
                  (check (not= :blocked (last @hits)) "Disposed shortcut cannot activate"))
                (check (try (w/focus! 9999999) false (catch Throwable _ true)) "Stale focus handle rejected")
                (reset! done true) (qt/quit!))]))))
     :auto-quit-ms 7000)
    (check @done "Checks completed")
    (check (zero? (w/live-count)) "Owned objects disposed")
    (println "PASS:" @checks "keyboard checks")
    (finally (shutdown-agents))))
