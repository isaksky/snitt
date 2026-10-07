(ns glimmer-qt.values-test
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.value :as v]
            [glimmer-qt.widget :as w]
            [jolt.host :as host]
            [jolt.ffi :as ffi]))
(ffi/defcfn fixture "gqt_value_fixture" [:uint64] :uint64)
(ffi/defcfn reuse-object "gqt_reuse_test_object" [:uint64] :uint64)
(def checks (atom 0))
(defn check! [yes message]
  (swap! checks inc)
  (when-not yes (throw (ex-info message {}))))
(defn throws! [f pattern]
  (check! (try (f) false (catch Throwable e (boolean (re-find pattern (ex-message e)))))
          (str "Expected error: " pattern)))
(defn roundtrip! [id prop value]
  (q/set-property! id prop value)
  (check! (= value (q/read-property id prop)) (str "Round trip " prop " " value)))
(defn run! []
  (let [s (q/open "Typed values" 320 240 "glimmer.values.test")]
    (try
      (let [id (fixture s)
            label (q/create s "label")
            child (q/create s "label")]
        (doseq [n [-9223372036854775808 -9007199254740993 0 9007199254740993 9223372036854775807]]
          (roundtrip! id "duration" n))
        (doseq [[prop value] [["text" "Jolt 🌍 日本語 \"quotes\" \\ /\n\t\r\b\f\u0000"]
                              ["count" -2147483648] ["count" 2147483647]
                              ["busy" true] ["busy" false] ["progress" 0.125]
                              ["color" (v/color "#80abcdef")]
                              ["point" (v/point -1920 -1080)] ["size" (v/size 640 480)]
                              ["rect" (v/rect -1920 -75 640 480)]
                              ["pointF" (v/point-f -10.25 0.5)] ["sizeF" (v/size-f 100.5 77.25)]
                              ["rectF" (v/rect-f -1920.25 -100.75 640.5 480.125)]
                              ["list" [true 9223372036854775807 "🌍" nil]]
                              ["strings" ["日本語" "a/b"]]
                              ["settings" {"nested" {"n" -9223372036854775808 "flag" false
                                                       "rect" (v/rect-f -1.25 0.5 20.0 30.5)}
                                           "list" [nil 1 "🌍" {"url" (v/url "https://example.org/a%20b?q=%23")}] }]
                              ["mode" (v/enum "Busy")]
                              ["options" (v/flags "None")]
                              ["options" (v/flags "First" "Second")]
                              ["object" (v/object child)]]]
          (roundtrip! id prop value))
        (doseq [x [1 1.0 "1" true]]
          (roundtrip! id "settings" {"typed" [{"number" x}]}))
        (let [path "/tmp/日本語 🌍 # ? % & + ' \".png"]
          (q/set-property! id "source" (v/local-file path))
          (let [url (q/read-property id "source")]
            (check! (= :url (:type url)) "Typed URL return")
            (check! (= path (q/local-file-path url)) "Special characters in local URL")
            (roundtrip! id "source" url)))
        (q/set-property! id "count" 99)
        (q/reset-property! id "count")
        (check! (= 7 (q/read-property id "count")) "Native RESET default")
        (q/reset-property! id "text" "explicit default")
        (check! (= "explicit default" (q/read-property id "text")) "Explicit default reset")
        (throws! #(q/reset-property! id "text") #"no native RESET")
        (throws! #(q/reset-property! id "readOnly") #"Read-only")
        (doseq [[prop value pattern] [["missing" 1 #"Missing"] ["readOnly" "fixed" #"Read-only"]
                                     ["count" 2147483648 #"range"] ["duration" 1.0 #"type"]
                                     ["progress" 9007199254740993 #"range"] ["busy" 1 #"type"]
                                     ["color" "red" #"type"] ["color" (v/color "bogus") #"color"]
                                     ["mode" (v/enum "Bogus") #"enum/flag"] ["mode" 1 #"symbolic"]
                                     ["options" (v/flags "First" "Bogus") #"enum/flag"]
                                     ["options" (v/enum "First") #"symbolic"]
                                     ["strings" ["ok" 1] #"strings"]
                                     ["object" 123 #"reference"] ["rect" (v/rect-f 1 2 3 4) #"type"]]]
          (let [before (when-not (= prop "missing") (q/read-property id prop))]
            (throws! #(q/set-property! id prop value) pattern)
            (when-not (= prop "missing") (check! (= before (q/read-property id prop)) "Failed write preserves value"))))
        (throws! #(q/read-property id "missing") #"Missing")
        (throws! #(q/read-property id "unsupported") #"Unsupported")
        (doseq [bad [#{1} :keyword {1 "bad"} 9223372036854775808N ##NaN ##Inf
                     (v/point 1.5 2) (v/rect 0 0 2147483648 1)]]
          (throws! #(q/set-property! id "settings" bad) #"Unsupported or invalid"))
        (throws! #(q/write-value id "count" "[\"int\",\"9223372036854775808\"]") #"int64")
        (throws! #(q/write-value id "count" "[]") #"Malformed")
        (throws! #(q/write-value id "count" "[\"bogus\",\"\"]") #"Unsupported")
        (roundtrip! id "settings" {"reference" [(v/object child)]})
        (throws! #(q/set-property! id "text\u0000suffix" "bad") #"property name")
        (throws! #(q/read-property 18446744073709551617N "text") #"handle")
        (throws! #(q/write-value id "mode" "[\"enum\",[\"Busy\\u0000suffix\"]]") #"enum/flag")
        (q/remove-widget child)
        (throws! #(q/read-property id "settings") #"disposed")
        (throws! #(q/set-property! id "object" (v/object child)) #"disposed")
        (throws! #(q/set-property! id "settings" {"child" (v/object child)}) #"disposed")
        (throws! #(q/read-property id "object") #"disposed")
        (q/set-property! id "object" nil)
        (check! (nil? (q/read-property id "object")) "Null object reference")
        (throws! #(q/read-property child "text") #"disposed")
        (let [old (q/owner s)]
          (q/set-property! id "settings" {"ref" (v/object old)})
          (let [replacement (reuse-object old)]
            (check! (and (pos? replacement) (not= old replacement)) "Reused address has new identity")
            (throws! #(q/read-property id "settings") #"disposed")
            (roundtrip! id "settings" {"ref" (v/object replacement)})))
        ;; Qt's width RESET restores implicit sizing; verify it keeps tracking.
        (q/set-property! label "text" "short")
        (let [implicit (q/read-property label "implicitWidth")]
          (q/set-property! label "width" 800.0)
          (q/reset-property! label "width")
          (check! (= implicit (q/read-property label "width")) "RESET restores implicit width")
          (q/set-property! label "text" "a substantially longer label")
          (check! (> (q/read-property label "width") implicit) "Reset width follows implicit size"))
        ;; An equal write must not remove a live QML binding.
        (let [entry (q/create s "entry")]
          (q/set-property! entry "hint" "first")
          (q/set-property! entry "placeholderText" "first")
          (q/set-property! entry "hint" "second")
          (check! (= "second" (q/read-property entry "placeholderText")) "Equal write preserves QML binding"))
        (reset! w/session s)
        (let [widget (w/create! :label {:label "props" :color "red" :margin 10})]
          (w/apply-props! :label widget {})
          (check! (= "" (q/read-property widget "text")) "Removed text default")
          (check! (= (v/color "#ffffffff") (q/read-property widget "color")) "Removed typed color default")
          (check! (= 0.0 (q/read-property widget "Layout.margins")) "Removed numeric default")
          (w/dispose! widget)))
      (finally (q/close s) (w/clear!)))))
(defn -main [& _]
  (host/call-on-main-thread run!)
  (println "PASS:" @checks "typed Qt property checks"))
