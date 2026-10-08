(ns glimmer-qt.text-test
  (:require [glimmer-qt.core :as qt] [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q] [glimmer.ratom :as r] [jolt.ffi :as ffi]))
(ffi/defcfn key! "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn ime! "gqt_test_ime" [:uint64 :int] :int :blocking)
(ffi/defcfn pointer! "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(ffi/defcfn paste-native! "gqt_test_paste" [:uint64 :pointer] :int :blocking)
(defn paste! [window text] (ffi/with-c-string [s text] (paste-native! window s)))
(def checks (atom 0))
(def done (atom false))
(def changes (atom []))
(def commits (atom 0))
(def cancels (atom 0))
(def wheels (atom 0))
(def heights (atom []))
(def old-height (atom 0))
(def editor (atom nil))
(def model (r/atom "A😀日本\né line"))
(def size (r/atom 16))
(def unrelated (r/atom 0))
(defn check [v msg] (swap! checks inc) (when-not v (throw (ex-info msg {}))))
(defn id [s] (w/find-by-id s))
(defn readp [p] (q/read-property (id "edit") p))
(defn props [] {:id "edit" :text @model :font-size @size
               :on-change #(do (swap! changes conj %) (reset! model %))
               :on-commit #(swap! commits inc) :on-cancel #(swap! cancels inc)
               :on-wheel (fn [_] (swap! wheels inc) true)
               :qt/subscriptions {:height {:property "contentHeight" :handler #(swap! heights conj %)}}})
(defn root []
  [:vbox {:hexpand true :vexpand true}
   [:label {:id "caption" :label (str @unrelated)}]
   [:scroll {:id "scroll" :hexpand true :vexpand true :on-wheel (fn [_] (swap! wheels inc) true)} [:text-area (props)]]])
(defn steps [fs] (when-let [f (first fs)] (qt/later! 100 #(do (f) (steps (rest fs))))))
(defn -main [& _]
  (try
    (qt/run-application!
     #(let [window (qt/open-window! root :width 250 :height 160)]
        (qt/later! 300
          (fn []
            (qt/activate! window) (w/focus! (id "edit"))
            (steps
             [(fn []
                (reset! editor (id "edit"))
                (check (empty? @changes) "Initial text is not user input")
                (w/apply-props! :text-area (id "edit") (assoc (props) :selection-anchor 5 :cursor-position 7))
                (check (= "é" (readp "selectedText")) "Combining mark uses a separate codepoint")
                (w/apply-props! :text-area (id "edit") (assoc (props) :selection-anchor 2 :cursor-position 1))
                (check (= 1 (readp "cursorPosition")) "Reverse selection retains cursor direction")
                (w/apply-props! :text-area (id "edit") (assoc (props) :selection-anchor 1 :cursor-position 2))
                (check (= "😀" (readp "selectedText")) "Codepoint selection spans surrogate pair")
                (check (= 3 (readp "cursorPosition")) "Native cursor uses UTF-16")
                (swap! unrelated inc) (reset! size 24))
              (fn []
                (check (= @editor (id "edit")) "Rerender retains native identity")
                (check (= "😀" (readp "selectedText")) "Rerender and style preserve selection")
                (check (empty? @changes) "Font change is not user input")
                (check (= 2 (readp "cursorCodepoint")) "Codepoint cursor reporting")
                (paste! window "🦊\n日本")
                (check (= "A🦊\n日本日本\né line" (readp "text")) "Paste replaces emoji selection")
                (check (= 1 (count @changes)) (str "Paste emits one user change: " @changes))
                (ime! (id "edit") 0)
                (check (readp "inputMethodComposing") "IME preedit active")
                (swap! unrelated inc) (reset! size 20))
              (fn []
                (check (readp "inputMethodComposing") "Rerender/font change preserve composition")
                (check (= "に" (readp "preeditText")) "Preedit intact")
                (ime! (id "edit") 1)
                (check (= "A🦊\n日本日本日本\né line" (readp "text")) "IME commits at the exact Unicode cursor")
                (key! window 16777220 67108864)
                (key! window 16777216 0)
                (check (= 1 @commits) "Platform commit fires once")
                (check (= 1 @cancels) "Escape cancels once")
                (key! window 65 67108864) (key! window 16777219 0)
                (check (= "" (readp "text")) "Select all/delete produces empty input")
                (check (= "" (last @changes)) "Empty input reported")
                (reset! model (apply str (repeat 50 "Long 😀 日本 annotation text\n"))))
              (fn []
                (check (> (readp "contentHeight") 160) "Long Unicode text wraps beyond frame")
                (check (seq @heights) "Content height notifications reach Glimmer")
                (reset! old-height (readp "contentHeight"))
                ;; With the inherited platform font, 250 and 170 can both put
                ;; this fixture on two lines. Cross a full additional wrap.
                (q/set-property! window "width" 120))
              (fn []
                (check (> (readp "contentHeight") @old-height)
                       (str "Narrower frame rewraps text: " @old-height " -> " (readp "contentHeight")
                            " at width " (readp "width")))
                (let [viewport (q/child-target (id "scroll") "contentItem")]
                  (q/set-property! viewport "contentY" 200)
                  (pointer! (id "scroll") 4 30.0 40.0)
                  (check (= 200.0 (q/read-property viewport "contentY")) "Consumed wheel does not scroll"))
                (w/apply-props! :scroll (id "scroll") {:id "scroll" :hexpand true :vexpand true})
                (w/apply-props! :text-area (id "edit") (dissoc (props) :on-wheel))
                (pointer! (id "scroll") 5 30.0 40.0))
              (fn []
                (check (< (q/read-property (q/child-target (id "scroll") "contentItem") "contentY") 200)
                       "Unconsumed wheel scrolls the viewport")
                (let [before (count @changes)]
                  (w/apply-props! :text-area (id "edit") (assoc (props) :text "programmatic"))
                  (check (= before (count @changes)) "Programmatic assignment is silent"))
                (w/apply-props! :scroll (id "scroll")
                  {:id "scroll" :hexpand true :vexpand true
                   :on-wheel (fn [_] (swap! wheels inc) true)}))
              (fn []
                (w/focus! (id "caption"))
                (pointer! (id "scroll") 1 30.0 40.0)
                (pointer! (id "scroll") 0 30.0 40.0)
                (pointer! (id "scroll") 2 30.0 40.0)
                (check (readp "activeFocus") "Mouse click reaches text through wheel overlays")
                (pointer! (id "scroll") 5 30.0 40.0)
                (check (= 2 @wheels) (str "Wheel routed to font policy: " @wheels))
                (reset! done true) (qt/quit!))]))))
     :auto-quit-ms 5000)
    (check @done "All steps completed")
    (check (zero? (w/live-count)) "Controls disposed")
    (println "PASS:" @checks "text checks")
    (finally (shutdown-agents))))
