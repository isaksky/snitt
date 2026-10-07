(ns glimmer-qt.pointer-test
  (:require [glimmer-qt.core :as qt] [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q] [glimmer-qt.value :as v] [jolt.ffi :as ffi]))
(ffi/defcfn native-input "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(defn input [id action x y] (native-input id action (double x) (double y)))
(w/register! :pointer-scroll
  {:source (str "file://" (System/getProperty "user.dir") "/test/qml/PointerScroll.qml")
   :props {:id ["objectName" :string "scroll"]
           :hexpand ["Layout.fillWidth" :bool true] :vexpand ["Layout.fillHeight" :bool true]}
   :children "contentItem"})
(def events (atom []))
(def checks (atom 0))
(def done (atom false))
(defn check [v message] (swap! checks inc) (when-not v (throw (ex-info message {:events @events}))))
(defn record [kind] (fn [e] (swap! events conj [kind e]) true))
(defn props [generation]
  {:id "drag" :x 100 :y 80 :width 80 :height 60 :scale 2 :hover true
   :prevent-stealing true :cursor (v/enum "SizeHorCursor")
   :on-pointer-press (record generation) :on-pointer-move (record :move)
   :on-pointer-release (record :release) :on-pointer-cancel (record :cancel)
   :on-hover-enter (record :enter) :on-hover-leave (record :leave)
   :on-wheel (fn [e] (swap! events conj [:wheel e]) (pos? (:pixel-y e)))})
(defn root []
  [:pointer-area {:id "parent" :hexpand true :vexpand true
                  :on-pointer-press (record :parent-press)
                  :on-pointer-release (record :parent-release)
                  :on-wheel (record :parent-wheel)}
   [:pointer-area (props :press)]])
(defn kinds [] (mapv first @events))
(defn event [kind] (second (first (filter #(= kind (first %)) @events))))
(defn -main [& _]
  (try
    (qt/run-application!
     #(do (qt/open-window! root :width 500 :height 350)
          (qt/later! 250
            (fn []
              (let [child (w/find-by-id "drag") parent (w/find-by-id "parent")]
                (input child 6 0 0)
                (input child 1 10 12)
                (input child 0 10 12)
                (input child 1 100 90)
                (input child 2 100 90)
                (check (some #{:enter} (kinds)) "Real hover entry")
                (check (some #{:leave} (kinds)) "Real hover exit")
                (check (= 1 (count (filter #{:press} (kinds)))) "Press once through hit testing")
                (check (= [10.0 12.0] ((juxt :x :y) (event :press))) "Scaled local coordinates")
                (check (= (q/map-point child nil 10 12) ((juxt :global-x :global-y) (event :press))) "Global coordinates")
                (when (= "offscreen" (System/getenv "QT_QPA_PLATFORM"))
                  (check (neg? (:global-x (event :press))) "Negative desktop origin"))
                (check (= 1 (:button (event :press))) "Button field")
                (check (pos? (:modifiers (event :press))) "Modifiers field")
                (check (= [100.0 90.0] ((juxt :x :y) (event :release))) "Grab retains drag outside bounds")
                (check (= [80.0 74.0] (q/map-point child parent 10 12)) "Map transformed items")
                (input child 0 10 12) (input child 3 0 0)
                (check (= 1 (count (filter #{:cancel} (kinds)))) "Real grab cancellation")
                (input child 2 10 12)
                (reset! events [])
                (input child 4 10 12)
                (check (= [:wheel] (kinds)) "Pixel wheel consumed by child")
                (check (= 7 (:pixel-y (event :wheel))) "Pixel delta preserved")
                (reset! events []) (input child 5 10 12)
                (check (= [:wheel :parent-wheel] (kinds)) "Unrelated wheel propagates to parent")
                (check (= 120 (:angle-y (event :wheel))) "Angle delta preserved")
                (w/apply-props! :pointer-area child (props :fresh))
                (reset! events []) (input child 0 10 12) (input child 2 10 12)
                (check (= [:fresh :release] (filterv #{:fresh :press :release} (kinds))) "Handler replaced once in place")
                (input child 0 10 12)
                (w/apply-props! :pointer-area child (assoc (props :fresh) :sensitive false))
                (check (some #{:cancel} (kinds)) "Disabling an active grab cancels it")
                (input child 2 10 12)
                (reset! events []) (input child 0 10 12) (input child 2 10 12)
                (check (not-any? #{:fresh :press :release} (kinds)) "Disabled child receives no input")
                (w/apply-props! :pointer-area child (props :fresh))
                (input child 0 10 12)
                (reset! events []) (w/dispose! child)
                (input parent 1 100 90) (input parent 2 100 90)
                (check (empty? @events) "Disposed callbacks removed during active grab")
                (check (try (q/map-point child parent 0 0) false (catch Throwable _ true)) "Stale mapping rejected")
                (qt/open-window!
                 (fn [] [:pointer-scroll {:id "scroll"}
                         [:pointer-area (assoc (props :scroll-press) :id "scroll-drag" :scale 1)]])
                 :width 400 :height 300)
                (qt/later! 250
                  (fn []
                    (let [drag (w/find-by-id "scroll-drag") scroll (w/find-by-id "scroll")]
                      (reset! events [])
                      (input drag 0 10 50)
                      (input drag 1 10 30) (input drag 1 10 -20) (input drag 1 10 -60)
                      (input drag 2 10 -60)
                      (check (some #{:release} (kinds)) "Prevent stealing retains drag in Flickable")
                      (check (not-any? #{:cancel} (kinds)) "Flickable did not cancel protected grab")
                      (check (zero? (q/read-property scroll "contentY")) "Protected drag does not scroll parent")
                      (q/set-property! scroll "contentY" 300)
                      (q/set-property! drag "offsetY" 380)
                      (reset! events [])
                      (input drag 4 10 12)
                      (check (some #{:wheel} (kinds)) "Touchpad reaches child inside Flickable")
                      (check (= 300.0 (q/read-property scroll "contentY")) "Consumed touchpad wheel does not scroll Flickable")
                      (input drag 5 10 12)
                      (qt/later! 250
                        (fn []
                          (check (< (q/read-property scroll "contentY") 300) "Ignored wheel scrolls real Flickable")
                          (reset! done true) (qt/quit!)))))))))) :auto-quit-ms 8000)
    (check @done "Completed")
    (check (zero? (w/live-count)) "Disposed all items")
    (println "PASS:" @checks "pointer checks")
    (finally (shutdown-agents))))
