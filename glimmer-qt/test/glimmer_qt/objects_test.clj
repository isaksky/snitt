(ns glimmer-qt.objects-test
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as o]
            [glimmer-qt.value :as v]
            [glimmer-qt.widget :as w]
            [glimmer-qt.lifecycle-test :refer [check! throws! clean! assertions run-steps! widget]]
            [jolt.host :as host]
            [jolt.ffi :as ffi]))
(ffi/defcfn register-fixture "gqt_register_object_fixture" [] :int)
(ffi/defcfn reuse-object "gqt_reuse_test_object" [:uint64] :uint64)
(ffi/defcfn awaiting-gc "gqt_test_awaiting_gc" [] :int)
(ffi/defcfn gc-completed "gqt_test_gc_completed" [] :void)
(def registered (atom false))
(def payload "payload(int,QVariantList,QUrl,QObject*)")
(w/register! :test/service {:source "native:test.object" :children :self
                           :relay "relay" :events {:on-change :string}
                           :props {:id ["objectName" :string ""] :count ["count" :number 0]
                                   :reference ["reference" :value nil]}})
(w/register! :test/container {:source "native:test.object" :children "child"})
(defn init! []
  (when-not @registered
    (check! (= 1 (register-fixture)) "Register fixture on GUI thread")
    (reset! registered true)))
(defn service! []
  (let [id (q/create-registered @w/session "native:test.object" (v/encode {}) "")]
    (o/invoke! id "exposeChild()" [])
    id))
(defn clean-objects! []
  (clean! [])
  (check! (zero? (o/live-count)) "All managed signal closures released")
  (host/call-on-main-thread #(check! (zero? (q/subscription-count)) "All native subscriptions released")))
(defn run! [ready]
  (let [done (atom false)]
    (ui/run (fn [] (init!) [:label]) :auto-quit-ms 5000
            :on-ready #(ready (fn [] (reset! done true) (qt/quit!))))
    (check! @done "Object test completed before deadline")
    (clean-objects!)))

(defn methods! []
  (run!
   (fn [done]
     (let [id (service!) child (q/read-property id "child")]
       (check! (q/alive? (:value child)) "Native-owned child has a stable exposed handle")
       (check! (= child (q/read-property id "child")) "Repeated object read preserves identity")
       (let [holder (w/create! :test/service {:reference child})
             replacement (service!)]
         (check! (= child (q/read-property holder "reference")) "Object-valued initial prop uses typed schema")
         (w/apply-props! :test/service holder {:reference (v/object replacement)})
         (check! (= (v/object replacement) (q/read-property holder "reference")) "Object-valued prop replacement")
         (w/apply-props! :test/service holder {})
         (check! (nil? (q/read-property holder "reference")) "Removed object prop resets to nil")
         (w/dispose! holder)
         (check! (q/alive? (:value child)) "Reference assignment does not transfer native ownership")
         (check! (q/alive? replacement) "Reference replacement does not dispose the referent"))
       (doseq [[signature args expected]
               [["overloaded(int)" [41] 42] ["overloaded(QString)" ["日本語"] "日本語!"]
                ["duration(qlonglong)" [9007199254740993] 9007199254740993]
                ["enabled(bool)" [false] true] ["scale(double,double)" [1.5 4] 6.0]
                ["url(QUrl)" [(v/url "https://example.org/a%20b")] (v/url "https://example.org/a%20b")]
                ["list(QVariantList)" [[true nil 9007199254740993 child]] [true nil 9007199254740993 child]]
                ["strings(QStringList)" [["a" "日本語"]] ["a" "日本語"]]
                ["map(QVariantMap)" [{"nested" [child 1]}] {"nested" [child 1]}]
                ["variant(QVariant)" [{"url" (v/url "file:///tmp/a")}] {"url" (v/url "file:///tmp/a")}]
                ["object(QObject*)" [child] child] ["object(QObject*)" [nil] nil]
                ["typedObject(ObjectFixture*)" [(v/object id)] (v/object id)]
                ["change(int)" [8] nil]]]
         (check! (= expected (o/invoke! id signature args)) (str "Typed method " signature)))
       (doseq [[signature args pattern]
               [["overloaded" [1] #"explicit signature"] ["overloaded(bool)" [true] #"Missing"]
                ["overloaded(int)" [] #"argument count"] ["overloaded(int)" [2147483648] #"range"]
                ["overloaded(int)" ["bad"] #"type"] ["enabled(bool)" [1] #"type"]
                ["typedObject(ObjectFixture*)" [child] #"class"]
                ["strings(QStringList)" [["ok" 1]] #"strings"]
                ["changed()" [] #"public method"] ["unsupported()" [] #"Unsupported"]
                ["bytes(QByteArray)" ["a"] #"Unsupported"] ["throws()" [] #"fixture method failure"]
                ["change(int)\u0000suffix" [1] #"property name"]]]
         (throws! #(o/invoke! id signature args) pattern))
       (check! (= 8 (q/read-property id "count")) "Rejected invocations have no side effects")
       (throws! #(q/remove-widget (:value child)) #"borrowed")
       (throws! #(q/destroy-owner (:value child)) #"borrowed")
       (throws! #(q/append id (:value child)) #"borrowed")
       (throws! #(q/destroy-native (:value child)) #"borrowed")
       (throws! #(o/invoke! (:value child) "deleteLater()" []) #"borrowed")
       (doseq [[name prop? pattern] [["missing()" false #"Missing"] ["payload" false #"explicit"]
                                    ["bytesSignal(QByteArray)" false #"Unsupported"]
                                    ["child" true #"NOTIFY"] ["missing" true #"Missing"]]]
         (throws! #((if prop? o/observe! o/subscribe!) id id name identity) pattern))
       (check! (zero? (o/live-count)) "Failed subscriptions retain no closures")
       (let [collector (future
                         (loop [attempts 2000]
                           (cond
                             (= 1 (awaiting-gc)) (do (System/gc) (gc-completed) :collected)
                             (zero? attempts) :timeout
                             :else (do (Thread/sleep 1) (recur (dec attempts))))))]
         (check! (o/invoke! id "awaitGc()" []) "Worker GC completes before native invocation returns")
         (check! (= :collected (deref collector 2000 :timeout)) "Native invocation allows worker GC"))
       (let [worker (future (try (o/invoke! id "change(int)" [10]) :wrong
                                (catch Throwable e (ex-message e))))]
         (check! (re-find #"main thread" (deref worker 2000 "timeout")) "Methods reject worker-thread calls"))
       (o/invoke! id "removeChild()" [])
       (check! (not (q/alive? (:value child))) "Native deletion invalidates borrowed handle")
       (throws! #(o/invoke! (:value child) "deleteLater()" []) #"disposed")
       (check! (nil? (q/read-property id "child")) "Native QPointer property becomes nil")
       (throws! #(o/disconnect! 18446744073709551617N) #"handle")
       (done)))))

(defn fresh-and-replacement! []
  (let [a (atom nil) b (atom nil) owner (atom nil) calls (atom [])
        props (fn [target version] {:qt/subscriptions {:event {:target target :signal payload
                                                              :handler (fn [n & _] (swap! calls conj [version n]))}}})]
    (run-steps!
     (fn [] (init!) [:label])
     [(fn []
        (reset! a (service!)) (reset! b (service!))
        (reset! owner (w/create! :label (props @a :old)))
        (o/invoke! @a "fire(int)" [1])
        (check! (empty? @calls) "Signal delivery is never inline")
        (w/apply-props! :label @owner (props @a :fresh))
        (check! (= 1 (q/read-property @a "connects")) "Handler refresh keeps the native connection"))
      (fn []
        (check! (= [[:fresh 1]] @calls) "Queued signal uses fresh handler")
        (o/invoke! @a "fire(int)" [2])
        (w/apply-props! :label @owner (props @b :replacement))
        (check! (= 1 (q/read-property @a "disconnects")) "Target replacement disconnects exactly once")
        (o/invoke! @b "fire(int)" [3]))
      (fn []
        (check! (= [[:fresh 1] [:replacement 3]] @calls) "Replaced target's queued event is inert")
        (o/invoke! @b "fire(int)" [4])
        (w/dispose! @owner)
        (check! (= 1 (q/read-property @b "disconnects")) "Owner disposal disconnects exactly once")
        (check! (zero? (o/live-count)) "Unmount releases managed handlers immediately"))
      (fn [] (check! (= 2 (count @calls)) "No late callback after owner disposal"))])
    (clean-objects!)))

(defn reactive-and-controlled! []
  (let [state (r/atom 0) events (atom []) legacy-events (atom []) show (r/atom true) old (atom nil)]
    (run-steps!
     (fn []
       (init!)
       [:vbox
        (when @show
          [:test/service {:id "service" :count @state
                          :on-change #(swap! legacy-events conj %)
                          :qt/subscriptions {:state {:property "count" :handler #(do (swap! events conj %) (reset! state %))}}}
           [:test/container [:test/service {:id "nonvisual-child"}]]])
        [:label {:id "value" :label (str @state)}]])
     [(fn [] (reset! old (widget "service"))
        (check! (= 0 (q/is-item (widget "nonvisual-child"))) "Nonvisual QObject children mount under a service")
        (o/invoke! @old "change(int)" [7]))
      (fn [] (check! (= [7] @events) "Property notification feeds reactive state")
        (check! (= ["7"] @legacy-events) "Native method retains legacy relay delivery")
        (check! (= "7" (q/read-property (widget "value") "text")) "Signal-driven atom rerenders Glimmer")
        (reset! state 9))
      (fn []
        (check! (= 9 (q/read-property @old "count")) "Controlled update reaches native property")
        (check! (= [7] @events) "Controlled write causes no subscription feedback")
        (check! (= ["7"] @legacy-events) "Controlled write also suppresses legacy relay feedback")
        (check! (= 1 (q/read-property @old "connects")) "Rerender preserves observer connection")
        (reset! show false))
      (fn []
        (check! (not (q/alive? @old)) "Reconciler unmount invalidates service")
        (check! (zero? (o/live-count)) "Reconciler unmount disconnects observers"))])
    (clean-objects!)
    (clean! [state show])))

(defn workers-and-native-destruction! []
  (run!
   (fn [done]
     (let [id (service!) owner (qt/owner!) calls (atom []) self (atom nil) once (atom 0)
           child (q/read-property id "child")]
       (o/subscribe! owner id payload
                     (fn [n values url object]
                       (check! (= 1 (q/gui-thread)) "Worker signal enters Jolt on GUI thread")
                       (check! (= ["worker" 9007199254740993] values) "Worker list copied losslessly")
                       (check! (= (v/url "file:///tmp/worker%20value") url) "Worker URL preserved")
                       (check! (= child object) "Worker object identity preserved")
                       (swap! calls conj n)))
       (reset! self (o/subscribe! owner id payload (fn [& _] (swap! once inc) (o/disconnect! @self))))
       (doseq [n (range 20)] (o/invoke! id "fireWorker(int)" [n]))
       (System/gc)
       (qt/later! 100
                  #(do
                     (check! (= (set (range 20)) (set @calls)) "Every native worker signal delivered")
                     (check! (= 20 (count @calls)) "No duplicate signal delivery")
                     (check! (= 1 @once) "Callback can disconnect itself with queued events pending")
                     (o/disconnect! @self)
                     (q/destroy-native id)
                     (qt/later! 30
                                (fn []
                                  (check! (not (q/alive? id)) "Native target destruction invalidates handle")
                                  (check! (not (q/alive? (:value child))) "Native target destruction invalidates exposed child")
                                  (check! (zero? (o/live-count)) "Native target destruction releases subscriptions")
                                  (check! (zero? (q/subscription-count)) "Native subscriptions empty after target deletion")
                                  (done)))))))))

(defn failures-clean-up! []
  (throws!
   #(ui/run (fn [] (init!) [:label]) :auto-quit-ms 5000
            :on-ready (fn []
                        (let [id (service!)]
                          (o/subscribe! id id payload (fn [& _] (throw (ex-info "managed signal failed" {}))))
                          (o/invoke! id "fire(int)" [1]))))
   #"managed signal failed")
  (clean-objects!)
  (let [calls (atom 0)]
    (throws!
     #(ui/run (fn [] [:label]) :auto-quit-ms 5000
              :on-ready (fn []
                          (let [id (service!) old (q/owner @w/session)]
                            (o/subscribe! id id payload (fn [& _] (swap! calls inc)))
                            (o/invoke! id "fireObject(QObject*)" [(v/object old)])
                            (check! (not= old (reuse-object old)) "Reference address reused before queued delivery"))))
     #"disposed")
    (check! (zero? @calls) "Queued object reference never retargets a reused address")
    (clean-objects!)))

(defn -main [& _]
  (try
    (methods!)
    (fresh-and-replacement!)
    (reactive-and-controlled!)
    (workers-and-native-destruction!)
    (failures-clean-up!)
    (println "PASS:" @assertions "QObject method and subscription checks")
    (finally (shutdown-agents))))
