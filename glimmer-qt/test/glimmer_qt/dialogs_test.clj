(ns glimmer-qt.dialogs-test
  (:require [glimmer-qt.core :as qt] [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q] [glimmer-qt.value :as value]
            [jolt.ffi :as ffi]))
(ffi/defcfn key! "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn click! "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn file-result-native "gqt_test_file_result" [:uint64 :pointer] :int :blocking)
(defn file-result! [id path] (ffi/with-c-string [p path] (file-result-native id p)))
(def fixture (System/getenv "GQT_DIALOG_FIXTURE"))
(def checks (atom 0))
(def outcomes (atom []))
(def changes (atom []))
(def commands (atom 0))
(def done (atom false))
(defn check [v text] (swap! checks inc) (when-not v (throw (ex-info text {}))))
(defn id [s] (w/find-by-id s))
(defn visibility []
  (let [visible (w/dialog-visible? (id "confirm"))]
    (swap! changes conj visible)
    (w/apply-props! :shortcut (id "command")
      {:id "command" :sequences ["Ctrl+K"] :enabled (not visible)
       :on-activate #(swap! commands inc)})))
(defn root []
  [:vbox {:hexpand true :vexpand true}
   [:entry {:id "editor" :text "Editor"}]
   [:shortcut {:id "command" :sequences ["Ctrl+K"] :on-activate #(swap! commands inc)}]
   [:dialog {:id "error" :title "Capture failed" :on-rejected #(swap! outcomes conj :error-closed)}
    [:label {:label "An application supplied error message"}]
    [:button {:id "error-close" :label "Close" :on-click #(w/close-dialog! (id "error"))}]]
   [:dialog {:id "confirm" :title "Rearrange?" :on-visible-change visibility
             :on-accepted #(swap! outcomes conj :yes) :on-rejected #(swap! outcomes conj :no)}
    [:label {:label "Discard annotations?"}]
    [:hbox {}
     [:button {:id "cancel" :label "Cancel" :on-click #(w/reject-dialog! (id "confirm"))}]
     [:button {:id "accept" :label "Confirm" :on-click #(w/accept-dialog! (id "confirm"))}]]]
   [:file-chooser {:id "file" :selected-url (value/local-file fixture) :on-accepted #(swap! outcomes conj %) :on-rejected #(swap! outcomes conj :file-cancel)}]])
(defn steps [xs] (when-let [f (first xs)] (qt/later! 150 #(do (f) (steps (rest xs))))))
(defn automated []
  (try
    (qt/run-application!
     #(let [window (qt/open-window! root :title "Dialog verification")]
        (qt/later! 300
          (fn []
            (qt/activate! window)
            (w/focus! (id "editor"))
            (steps
             [(fn [] (w/open-dialog! (id "error")))
              (fn []
                (check (w/dialog-visible? (id "error")) "Error dialog visible")
                (click! (id "error-close")))
              (fn []
                (check (= [:error-closed] @outcomes) "Error close button rejects once")
                (check (q/read-property (id "editor") "activeFocus") "Error restores focus")
                (w/open-dialog! (id "confirm")))
              (fn []
                (check (w/dialog-visible? (id "confirm")) "Confirm visible")
                (key! window 75 67108864)
                (check (zero? @commands) "Modal visibility disables editor shortcut")
                (key! window 16777216 0))
              (fn []
                (check (= [:error-closed :no] @outcomes) "Escape rejects exactly once")
                (check (q/read-property (id "editor") "activeFocus") "Escape restores focus")
                (check (= [true false] @changes) (str "Visibility transitions " @changes))
                (key! window 75 67108864)
                (check (= 1 @commands) "Editor shortcut restored")
                (w/open-dialog! (id "confirm")))
              (fn [] (click! (id "accept")) (w/accept-dialog! (id "confirm")))
              (fn []
                (check (= [:error-closed :no :yes] @outcomes) "Confirm button delivers one acceptance")
                (w/open-dialog! (id "confirm")))
              (fn [] (click! (id "cancel")) (w/close-dialog! (id "confirm")))
              (fn []
                (check (= [:error-closed :no :yes :no] @outcomes) "Cancel button delivers one rejection")
                (check (q/read-property (id "editor") "activeFocus") "Buttons restore focus")
                (check (= ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"]
                          (q/read-property (id "file") "nameFilters")) "Supported image filters")
                (w/open-dialog! (id "file")))
              (fn []
                (check (w/dialog-visible? (id "file")) "Nonblocking chooser permits timer progress")
                (file-result! (id "file") fixture))
              (fn []
                (check (.endsWith (last @outcomes) "/space%20caf%C3%A9%20%E6%97%A5%E6%9C%AC.png") (str "Fully encoded local URL " @outcomes))
                (check (= fixture (q/local-file-path (value/url (last @outcomes)))) "URL round trip")
                (w/open-dialog! (id "file")))
              (fn [] (file-result! (id "file") "") (w/close-dialog! (id "file")))
              (fn []
                (check (= :file-cancel (last @outcomes)) "Chooser cancel")
                (check (= 6 (count @outcomes)) "Single file outcomes")
                (w/open-dialog! (id "file"))
                (qt/close-window! window))
              (fn []
                (check (= 6 (count @outcomes)) "Parent close suppresses callbacks")
                (check (zero? (w/live-count)) "Parent close releases managed widgets")
                (qt/open-window! root)
                (w/open-dialog! (id "file"))
                (reset! done true)
                (qt/quit!))]))))
     :auto-quit-ms 10000)
    (check @done "All dialog steps ran")
    (check (= 6 (count @outcomes)) "Shutdown suppresses callbacks")
    (check (zero? (q/owned-count)) "No owned native objects remain")
    (println "PASS" @checks "dialog checks")
    (System/exit 0)
    (catch Throwable e (println "FAIL" (ex-message e)) (System/exit 1))))

(defn desktop []
  (qt/run-application!
   #(let [window (qt/open-window!
                  (fn [] [:vbox {}
                          [:label {:label "Native chooser check: open the fixture image, then cancel."}]
                          [:file-chooser {:id "native" :title "Glimmer native image chooser verification"
                                          :selected-url (value/local-file fixture)
                                          :on-accepted (fn [url]
                                                         (check (= fixture (q/local-file-path (value/url url))) (str "Native selected URL " url " expected " fixture))
                                                         (check (.contains url "%20") "Native URL encodes spaces")
                                                         (check (.contains url "%C3%A9") "Native URL encodes Unicode")
                                                         (println "NATIVE ACCEPTED" url)
                                                         (qt/later! 300 (fn [] (w/open-dialog! (id "native")))))
                                          :on-rejected (fn [] (println "NATIVE CANCELLED") (reset! done true) (qt/quit!))}]])
                  :title "Native chooser desktop check")]
      (qt/later! 500 (fn [] (qt/activate! window) (w/open-dialog! (id "native"))))
      (qt/every! 1000 (fn [_] (println "GUI timer alive"))))
   :auto-quit-ms 180000)
  (check @done "Native check finished")
  (check (= 4 @checks) "Native acceptance and cancellation checked")
  (println "PASS native desktop chooser")
  (System/exit 0))

(defn -main [& args]
  (if (= "--native-desktop" (first args)) (desktop) (automated)))
