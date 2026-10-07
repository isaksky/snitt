(ns glimmer-qt.scheduling-test
  "Real Qt event-loop tests; no claims about desktop input. Runner has a deadline."
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]
            [glimmer-qt.lifecycle-test :refer [check! throws! clean! assertions]]
            [jolt.host :as host]))

(defn clean-scheduler! []
  (clean! [])
  (check! (zero? (q/pending-count)) "Native job registry empty after shutdown")
  (host/call-on-main-thread
   #(check! (zero? (q/timer-count)) "All native timers physically destroyed")))

(defn run! [ready]
  (let [done (atom false)]
    (ui/run (fn [] [:label]) :auto-quit-ms 5000
            :on-ready #(ready (fn [] (reset! done true) (qt/quit!))))
    (check! @done "Scheduling test timed out")
    (clean-scheduler!)))

(defn cancellation-and-owners! []
  (run!
   (fn [done]
     (let [calls (atom 0) work #(swap! calls inc)
           owner (qt/owner!)
           native-owner (qt/owner!)
           parent (w/create! :vbox {})
           child (w/create! :label {})
           baseline (qt/pending-count)]
       (w/append-child! :vbox parent child)
       (doseq [_ (range 100)]
         (let [token (qt/later! 60000 work)]
           (qt/cancel! token)
           (qt/cancel! token)
           (check! (false? (qt/restart! token)) "Canceled job cannot retain/restart a closure")))
       (qt/cancel! (qt/later! 0 work))
       (qt/later! owner 0 work)
       (qt/every! owner 1 (fn [_] (work)))
       (qt/later! child 0 work)
       (qt/every! parent 1 (fn [_] (work)))
       (qt/destroy-owner! owner)
       (w/dispose! parent)
       (check! (= baseline (qt/pending-count)) "Destroy owner/subtree releases closures immediately")
       (check! (= baseline (q/pending-count)) "Cancel removes native registrations immediately")
       (check! (nil? (qt/later! owner 0 work)) "Disposed owner rejects new work")
       (qt/later! native-owner 60000 work)
       (q/destroy-native native-owner)
       (throws! #(qt/every! 0 identity) #"positive")
       (throws! #(qt/later! -1 work) #"int32")
       (throws! #(qt/later! 2147483648 work) #"int32")
       (qt/later! 40
                  #(do (check! (zero? @calls) "No callbacks after cancel or owner destruction")
                       (check! (= baseline (qt/pending-count)) "Native owner destruction also releases closure")
                       (check! (= baseline (q/pending-count)) "Only session deadline remains")
                       ;; This callback's single-shot timer may await deferred deletion.
                       (check! (<= (q/timer-count) 2) "Canceled long timers are reclaimed before their deadline")
                       (done)))))))

(defn installed-timers! []
  (run!
   (fn [done]
     (let [calls (atom 0) owner (qt/owner!)
           canceled (qt/later! 60000 #(swap! calls inc))
           restarted (qt/later! 60000 #(swap! calls inc))]
       (qt/later! owner 60000 #(swap! calls inc))
       (qt/later! 20
                  #(do
                     (check! (>= (q/timer-count) 4) "Long timers were actually installed")
                     (qt/cancel! canceled)
                     (qt/destroy-owner! owner)
                     (check! (qt/restart! restarted 10) "Restart an installed native timer")
                     (qt/later! 50
                                (fn []
                                  (check! (= 1 @calls) "Only restarted installed timer fires")
                                  (check! (<= (q/timer-count) 2) "Canceled installed timers are physically reclaimed")
                                  (done)))))))))

(defn restart-and-repeat! []
  (run!
   (fn [done]
     (let [calls (atom 0) token (qt/later! 60000 #(swap! calls inc))
           ticks (atom []) repeat-token (atom nil)]
       (check! (qt/restart! token 20) "Restart a pending one-shot")
       (check! (qt/restart! token) "Restart using its current delay")
       (reset! repeat-token
               (qt/every! 8
                          (fn [time]
                            (check! (= 1 (q/gui-thread)) "Timer callback runs on Qt main thread")
                            (swap! ticks conj time)
                            (when (= 3 (count @ticks))
                              (qt/cancel! @repeat-token)
                              (qt/later! 40
                                         #(do
                                            (check! (= 1 @calls) "Restarted one-shot fires exactly once")
                                            (check! (false? (qt/restart! token)) "Completed one-shot cannot restart")
                                            (check! (= 3 (count @ticks)) "Repeat can cancel itself without late callbacks")
                                            (check! (apply < (map :elapsed-seconds @ticks)) "Elapsed time is monotonic")
                                            (check! (every? pos? (map :delta-seconds @ticks)) "Delta times are positive")
                                            (check! (< (abs (- (:elapsed-seconds (last @ticks))
                                                              (reduce + (map :delta-seconds @ticks)))) 0.000001)
                                                    "Deltas accumulate to total elapsed time")
                                            (done)))))))))))

(defn elapsed-and-restart! []
  (run!
   (fn [done]
     (let [token (atom nil) phase (atom :initial) initial (atom nil)]
       (reset! token
               (qt/every! 10
                          (fn [{:keys [elapsed-seconds delta-seconds]}]
                            (case @phase
                              :initial (do (reset! initial elapsed-seconds)
                                           (reset! phase :stalled)
                                           ;; Simulate no frames/GUI stall. Next tick measures real
                                           ;; time, rather than adding the nominal 10ms interval.
                                           (Thread/sleep 90))
                              :stalled (do
                                         (check! (> delta-seconds 0.07) "Playhead includes elapsed time during a static frame/stall")
                                         (check! (> elapsed-seconds @initial) "Total advances without frame events")
                                         (reset! phase :restarted)
                                         (check! (qt/restart! @token 15) "Restart repeat from inside its callback"))
                              :restarted (do
                                           (check! (= elapsed-seconds delta-seconds) "Restart resets elapsed and delta origin")
                                           (check! (pos? elapsed-seconds) "Restarted clock advances")
                                           (qt/cancel! @token)
                                           (done))))))))))

(defn workers-and-gc! []
  (let [state (r/atom 0) renders (atom 0) calls (atom 0) workers (atom []) done (atom false)]
    (ui/run (fn []
              (check! (= 1 (q/gui-thread)) "Reactive rendering stays on Qt main thread")
              (swap! renders inc)
              [:label {:id "counter" :label (str @state)}])
            :auto-quit-ms 7000
            :on-ready
            (fn []
              (reset! workers
                      (vec (for [_ (range 4)]
                             (future
                               (doseq [n (range 100)]
                                 (swap! state inc)
                                 (qt/later! 0 #(do (check! (= 1 (q/gui-thread)) "Worker post delivered on main thread")
                                                  (swap! calls inc)))
                                 (let [token (qt/every! 60000 identity)]
                                   (check! (qt/restart! token 60000) "Workers can restart")
                                   (qt/cancel! token))
                                 (when (zero? (mod n 20)) (System/gc)))
                               :finished))))
              ;; No event delivery during this burst: exactly one render should
              ;; be coalesced. Workers must finish even while the GUI waits/GCs.
              (doseq [worker @workers]
                (check! (= :finished (deref worker 4000 :timeout)) "Worker/GC completes within deadline"))
              (check! (= 1 @renders) "Workers never render inline")
              (check! (= 402 (qt/pending-count)) "400 posts plus one coalesced render and deadline")
              (qt/later! 100
                         #(do
                            (check! (= 2 @renders) "Concurrent ratom changes coalesce to one render")
                            (check! (= 400 @calls) "Every uncanceled worker post runs once")
                            (check! (= "400" (q/read-property (w/find-by-id "counter") "text")) "Latest ratom value rendered")
                            (reset! done true)
                            (qt/quit!)))))
    (check! @done "Worker test completed")
    (clean! [state])
    (clean-scheduler!)))

(defn shutdown-races! []
  (doseq [mode [:quit :window]]
    (let [workers (atom []) released (promise) calls (atom 0) old-session (atom nil)]
      (ui/run (fn [] [:label]) :auto-quit-ms 5000
              :on-ready
              (fn []
                (reset! old-session @w/session)
                (reset! workers
                        (vec (for [_ (range 4)]
                               (future
                                 @released
                                 (dotimes [_ 200]
                                   (qt/later! 60000 #(swap! calls inc)))
                                 :finished))))
                (deliver released true)
                (if (= mode :quit) (qt/quit!) (q/request-close @w/session))
                (check! (nil? (qt/later! 0 #(swap! calls inc))) "Shutdown closes admission immediately")))
      (doseq [worker @workers]
        (check! (= :finished (deref worker 4000 :timeout)) "Posting workers finish across shutdown"))
      (clean-scheduler!)
      (check! (zero? @calls) "No shutdown callbacks")
      (run! (fn [done]
              (check! (zero? (q/post @old-session 12345 0)) "Stale native session rejects posts")
              (qt/later! 30 #(do (check! (zero? @calls) "No old jobs enter next session") (done))))))))

(defn exception-policy! []
  (doseq [schedule [#(qt/later! 0 %) #(qt/every! 1 (fn [_] (%)))]]
    (throws! #(ui/run (fn [] [:label]) :auto-quit-ms 1000
                     :on-ready (fn [] (schedule (fn [] (throw (ex-info "scheduled failure" {}))))))
             #"scheduled failure")
    (clean-scheduler!)))

(defn -main [& _]
  (try
    (cancellation-and-owners!)
    (installed-timers!)
    (restart-and-repeat!)
    (elapsed-and-restart!)
    (workers-and-gc!)
    (shutdown-races!)
    (exception-policy!)
    (println "PASS:" @assertions "Qt scheduling checks")
    (finally (shutdown-agents))))
