(ns glimmer-qt.lifecycle-test
  "Binding-owned integration tests. Run with QT_QPA_PLATFORM=offscreen jolt -M:test.
  Uses actual Qt objects, signals, deferred deletion and event-loop scheduling;
  these are lifetime checks, not evidence of physical keyboard/mouse behavior."
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]
            [jolt.host :as host]))

(def assertions (atom 0))
(defn check! [value message]
  (swap! assertions inc)
  (when-not value (throw (ex-info message {}))))
(defn throws! [f message]
  (check! (try (f) false (catch Throwable e (boolean (re-find message (ex-message e)))))
          (str "Expected managed error: " message)))
(defn widget [id]
  (or (w/find-by-id id) (throw (ex-info "Missing control" {:id id}))))
(defn watches [cell] (count @(:watches cell)))
(defn step! [steps done]
  (if-let [f (first steps)]
    (qt/later! 12 #(do (f) (step! (rest steps) done)))
    (do (reset! done true) (q/request-close @w/session))))
(defn run-steps! [root steps]
  (let [done (atom false)]
    (ui/run root :title "Qt lifetime tests" :width 320 :height 240
            :on-ready #(step! steps done) :auto-quit-ms 7000)
    (check! @done "Lifecycle test timed out")))
(defn clean! [cells]
  (doseq [cell cells]
    (check! (zero? (watches cell)) "Subscriptions must detach before any later mutation"))
  (check! (zero? (w/live-count)) "Managed widget arena empty")
  (check! (zero? (qt/pending-count)) "Pending closures released")
  (check! (nil? @w/session) "Session released")
  (host/call-on-main-thread
   #(do (check! (zero? (q/live-count)) "Native handle arena empty")
        (check! (zero? (q/owned-count)) "All session-owned QObjects physically destroyed"))))

(defn reload-and-remount! []
  (let [state (r/atom 0) renders (atom 0) calls (atom []) stale (atom [])
        session (atom nil)
        root (fn []
               (swap! renders inc)
               (let [n @state]
                 [:button {:id "reload" :label (str n) :on-click #(swap! calls conj n)}]))]
    (run-steps!
     root
     (concat
      [(fn [] (reset! session @w/session))]
      (mapcat
       (fn [n]
         [(fn []
            (check! (= 1 (watches state)) "Exactly one current root subscription")
            (swap! stale conj (widget "reload"))
               ;; Queue a reload before a render of the old tree.
            (ui/reload!)
            (reset! state n))
          (fn []
            (check! (= (inc n) @renders) "Already-queued stale renders never execute")
            (check! (= 1 (watches state)) "Reload eagerly detaches old watcher")
            (check! (= 2 (w/live-count)) "Only window mount point and latest root remain")
            (check! (= 3 (q/live-count)) "Only window, mount point and current control remain")
            (doseq [id @stale] (check! (not (q/alive? id)) "Reload invalidates every old handle"))
            (q/emit-signal (widget "reload") "clicked")
            (check! (= n (last @calls)) "Only the fresh callback runs")
            (check! (= n (count @calls)) "No duplicate callback"))])
       (range 1 13))))
    (clean! [state])
    (let [before @renders]
      (reset! state 99)
      (ui/reload!)
      (check! (= before @renders) "Close clears the reload root and stale render jobs"))
    (run-steps! root
                [(fn []
                   (check! (not= @session @w/session) "Remount uses a new session identity")
                   (check! (= 0 (q/post @session 999 0)) "A stale worker post cannot enter a new session")
                   (doseq [id @stale]
                     (throws! #(q/set-string id "text" "stale") #"disposed"))
                   (check! (= "99" (q/read-property (widget "reload") "text")) "Remount uses same long-lived ratom"))])
    (clean! [state])))

(defn reconciliation! []
  (let [state (r/atom {:n 0 :extra true :show true :tag :label :rows [:a :b :c]})
        subtree (r/atom "child")
        conditional (r/atom "conditional")
        calls (atom []) old-button (atom nil) old-entry (atom nil)
        old-leaf (atom nil) old-b (atom nil)
        child (fn [] [:label {:id "subtree" :label @subtree}])
        root (fn []
               (let [{:keys [n extra show tag rows]} @state]
                 [:vbox {:id "host"}
                  [:button (merge {:id "button"}
                                  (when extra {:label (str @conditional n) :highlighted true
                                               :sensitive false :margin 9 :on-click #(swap! calls conj n)}))]
                  [:entry {:id "entry" :text "controlled"}]
                  [tag {:id "leaf" :label "leaf"}]
                  [:hbox {:id "rows"}
                   (for [key rows] [:label {:key key :id (name key) :label (name key)}])]
                  (when show [child])]))]
    (run-steps! root
                [(fn []
                   (reset! old-button (widget "button"))
                   (reset! old-entry (widget "entry"))
                   (reset! old-leaf (widget "leaf"))
                   (reset! old-b (widget "b"))
                   (q/emit-signal @old-button "clicked")
                   (swap! state assoc :n 1))
                 (fn []
                   (check! (= @old-button (widget "button")) "Handler changes preserve identity")
                   (q/emit-signal @old-button "clicked")
                   (check! (= [0 1] @calls) "Handler closure refreshed")
                   (q/set-string @old-entry "text" "unaccepted user edit")
                   (swap! state assoc :n 2 :extra false :tag :button :rows [:c :b :d] :show false))
                 (fn []
                   (check! (= @old-entry (widget "entry")) "Controlled entry reused")
                   (check! (= "controlled" (q/read-property @old-entry "text")) "Controlled prop restored even when unchanged")
                   (check! (= "" (q/read-property @old-button "text")) "Removed string prop resets")
                   (check! (= false (q/read-property @old-button "highlighted")) "Removed bool prop resets")
                   (check! (= true (q/read-property @old-button "enabled")) "Removed sensitivity resets")
                   (check! (= 0.0 (q/read-property @old-button "Layout.margins")) "Removed numeric prop resets")
                   (q/emit-signal @old-button "clicked")
                   (check! (= [0 1] @calls) "Removed handler is disconnected")
                   (check! (zero? (watches conditional)) "Conditional dependency detached")
                   (check! (zero? (watches subtree)) "Subtree subscriptions detached immediately")
                   (check! (nil? (w/find-by-id "subtree")) "Removed subtree unregistered")
                   (check! (not (q/alive? @old-leaf)) "Tag replacement disposes original native control")
                   (throws! #(w/apply-props! :label @old-leaf {}) #"disposed")
                   (check! (= @old-b (widget "b")) "Keyed child retained")
                   (check! (nil? (w/find-by-id "a")) "Removed keyed child invalidated")
                   (check! (< (q/read-property (widget "c") "x")
                              (q/read-property (widget "b") "x")
                              (q/read-property (widget "d") "x")) "Keyed visual order")
                   (reset! subtree "must not render")
                   (reset! conditional "must not render")
                   (swap! state assoc :show true))
                 #(check! (= 1 (watches subtree)) "Re-added subtree has exactly one subscription")])
    (clean! [state subtree conditional])))

(defn creation-and-handle-failures! []
  (let [cell (r/atom 0) old (atom [])]
    (run-steps! (fn [] [:vbox {:id "host"}])
                [(fn []
                   (let [host (widget "host") count-before (q/live-count)
                         child (fn [] @cell [:label {:label "ok"}])]
                     (throws! #(ui/mount host :vbox [:vbox [child] [:unsupported]]) #"Unsupported")
                     (check! (= count-before (q/live-count)) "Failed child mount releases prior native allocations")
                     (check! (zero? (watches cell)) "Partial creation failure releases prior child subscriptions")
                     ;; Initial properties now cross the native creation boundary,
                     ;; so exercise a real checked write failure after allocation.
                     (throws! #(w/create! :label {:font-size 2147483648}) #"numeric range")
                     (check! (= count-before (q/live-count)) "Failed property application disposes the new allocation")
                     (with-redefs [q/append (fn [& _] (throw (ex-info "injected attachment failure" {})))]
                       (throws! #(ui/mount host :vbox [:label]) #"attachment"))
                     (check! (= count-before (q/live-count)) "Failed attachment disposes unattached allocation")
                     (doseq [_ (range 120)]
                       (let [id (w/create! :button {:on-click #(throw (ex-info "stale handler" {}))})]
                         (check! (not (some #{id} @old)) "Never reuse a native handle")
                         (swap! old conj id)
                         (w/dispose! id)))
                     (doseq [id @old]
                       (throws! #(q/emit-signal id "clicked") #"disposed")
                       (throws! #(q/remove-widget id) #"disposed"))
                     (check! (= count-before (q/live-count)) "Detached disposal leaves no live handle")
                     (throws! #(q/read-property 99999999 "text") #"disposed")
                     (throws! #(w/apply-props! :button host {}) #"mismatch")
                     (throws! #(w/remove-child! :vbox host host) #"not a child")
                     (throws! #(q/close @w/session) #"executing")
                     (throws! #(w/dispose! (q/container @w/session)) #"mount point")
                     (check! @(future (try (q/read-property host "width") false
                                           (catch Throwable e (boolean (re-find #"main thread" (ex-message e))))))
                             "Off-thread native mutation rejected")))])
    (clean! [cell])))

(defn failure-teardown! []
  (let [cell (r/atom 0)
        root (fn [] @cell [:button {:id "fail" :on-click #(do (w/create! :label {})
                                                              (swap! cell inc)
                                                              (throw (ex-info "handler failure" {})))}])]
    (throws! #(ui/run root :on-ready (fn [] (q/emit-signal (widget "fail") "clicked")) :auto-quit-ms 1000)
             #"handler failure")
    (clean! [cell])
    (throws! #(ui/run (fn [] @cell [:vbox [:label] [:unsupported]]) :auto-quit-ms 1000) #"Unsupported")
    (clean! [cell])
    (throws! #(ui/run root :on-ready (fn [] (ui/reload! (fn [] @cell [:unsupported]))) :auto-quit-ms 1000)
             #"Unsupported")
    (clean! [cell])
    (doseq [target [:control :container]]
      (throws! #(ui/run root :on-ready (fn [] (q/destroy-native (if (= target :control)
                                                                  (widget "fail")
                                                                  (q/container @w/session))))
                        :auto-quit-ms 1000)
               #"destroyed a mounted object")
      (clean! [cell]))
    (reset! cell 1)
    (run-steps! root [#(check! (= 1 (watches cell)) "A failed session can remount cleanly")])
    (clean! [cell])))

(defn timer-ownership! []
  (let [calls (atom 0) queued (atom nil)]
    (run-steps! (fn [] [:label])
                [(fn []
                   (let [token (qt/later! 0 #(swap! calls inc))] (qt/cancel! token))
                   (reset! queued (future (qt/later! 60000 #(swap! calls inc)))))
                 (fn []
                   (check! (integer? @@queued) "Worker can enqueue session-owned work")
                   (check! (zero? @calls) "Canceled thunk did not run"))])
    (clean! [])
    (check! (nil? (qt/later! 0 #(swap! calls inc))) "No admission after close")
    (run-steps! (fn [] [:label]) [#(check! (zero? @calls) "No prior-session timer callback")])
    (clean! [])))

(defn -main [& _]
  (try
    (reload-and-remount!)
    (reconciliation!)
    (creation-and-handle-failures!)
    (failure-teardown!)
    (timer-ownership!)
    (println "PASS:" @assertions "Qt lifecycle checks")
    (finally (shutdown-agents))))
