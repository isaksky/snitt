(ns hello.extensions-test
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.value :as v]
            [hello.extensions :as ext]
            [jolt.host :as host]))
(def checks (atom 0))
(defn check! [x message] (swap! checks inc) (when-not x (throw (ex-info message {}))))
(defn throws! [f pattern]
  (check! (try (f) false (catch Throwable e (boolean (re-find pattern (ex-message e)))))
          (str "Expected error " pattern)))
(w/register! :hello/throws
  {:source "native:hello.service" :dispose #(throw (ex-info "hook failure" {}))})
(w/register! :hello/bad-slot
  {:source ext/spinner-source :relay "relay" :children "missingContent"
   :props {:angle ["angle" :number 0]}})
(def angle (r/atom 35))
(def tag (r/atom :hello/spinner))
(def rows (r/atom [:a :b]))
(def shown (r/atom true))
(def events (atom []))
;; Native tag uses exactly the schema it registered.
(defn root []
  (ext/install-native!)
  [:vbox
   (when @shown
     (let [n @angle]
       [@tag (cond-> {:id "custom" :angle n}
               (= @tag :hello/spinner) (assoc :on-activate #(swap! events conj [n %])))
        (for [key @rows] [:label {:key key :id (name key) :label (name key)}])]))])
(defn widget [id] (w/find-by-id id))
(defn property [id prop] (q/read-property (widget id) prop))
(defn steps! [steps]
  (when-let [f (first steps)] (qt/later! 25 #(do (f) (steps! (rest steps))))))
(defn -main [& _]
  (try
    (let [original (atom nil) child (atom nil) done (atom false)]
      (ui/run root :on-ready
        #(steps!
          [(fn []
             (reset! original (widget "custom"))
             (reset! child (widget "b"))
             (check! (= 35.0 (property "custom" "completedAngle")) "Required value before QML completion")
             (throws! (fn [] (w/create! :hello/spinner {})) #"Missing required")
             (throws! (fn [] (w/create! :hello/spinner {:angle 1 :typo true})) #"Unsupported")
             (let [before (q/live-count)]
               (throws! (fn [] (w/create! :hello/bad-slot {})) #"owned QObject")
               (check! (= before (q/live-count)) "Failed composition target releases created object")
               (throws! (fn [] (q/create-registered @w/session ext/spinner-source (v/encode {}) "relay"))
                        #"(?s)qrc:/hello/Spinner.qml:.*Required property angle")
               (check! (= before (q/live-count)) "Failed creation releases partial objects"))
             (let [service (w/create! :hello/service {:id "service"})]
               (check! (= "service" (q/read-property service "objectName")) "QObject factory initialized")
               (w/dispose! service)
               (check! (not (q/alive? service)) "QObject disposal invalidates handle"))
             (let [parent (w/create! :vbox {})
                   throwing (w/create! :hello/throws {})
                   label (w/create! :label {:label "sibling"})]
               (w/append-child! :vbox parent throwing)
               (w/append-child! :vbox parent label)
               (w/reorder-child! :vbox parent label throwing)
               (throws! (fn [] (w/dispose! parent)) #"hook failure")
               (check! (every? (fn [h] (not (q/alive? h))) [parent throwing label]) "Throwing hook releases whole subtree"))
             (reset! angle 90)
             (reset! rows [:b :a :c]))
           (fn []
             (check! (= @original (widget "custom")) "Custom QML reused")
             (check! (= @child (widget "b")) "Custom children reused")
             (check! (= 90.0 (property "custom" "angle")) "Prop update")
             (check! (< (property "b" "y") (property "a" "y") (property "c" "y")) "Composition target reorder")
             (q/emit-signal (widget "custom") "activated")
             (check! (= [[90 "spin"]] @events) "Fresh custom string handler")
             (reset! tag :hello/marker))
           (fn []
             (check! (not (q/alive? @original)) "QML replacement disposed")
             (check! (not (q/alive? @child)) "Nested children disposed")
             (reset! original (widget "custom"))
             (check! (= 90.0 (property "custom" "completedAngle")) "Native completion sees initial props")
             (reset! angle 180))
           (fn []
             (check! (= @original (widget "custom")) "Native item reused")
             (check! (= 180.0 (property "custom" "rotation")) "Native rotation update")
             (reset! shown false))
           (fn []
             (check! (not (q/alive? @original)) "Native subtree disposed")
             (check! (= 3 @ext/disposed) "Each disposal hook called once")
             (reset! done true)
             (qt/quit!))])
        :auto-quit-ms 5000)
      (check! @done "Extension test completed")
      (check! (zero? (w/live-count)) "Managed cleanup")
      (host/call-on-main-thread #(check! (zero? (q/owned-count)) "Physical QObject cleanup")))
    (println "PASS:" @checks "extension checks")
    (finally (shutdown-agents))))
