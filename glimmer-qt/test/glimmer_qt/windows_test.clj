(ns glimmer-qt.windows-test
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]
            [glimmer-qt.value :as v]
            [glimmer-qt.lifecycle-test :refer [check! throws! clean! watches]]
            [jolt.ffi :as ffi]))

(ffi/defcfn register-fixture "gqt_register_window_fixture" [] :int)
(ffi/defcfn click "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn space "gqt_test_space" [:uint64] :int :blocking)
(w/register! :test/widget {:source "native:test.widget"})

(defn steps! [steps]
  (when-let [step (first steps)]
    (qt/later! 100 #(do (step) (steps! (rest steps))))))

(defn independent-windows! []
  (let [a (atom nil) b (atom nil) service (atom nil)
        left (r/atom 0) right (r/atom 10) calls (atom [])
        closes (atom 0) accept? (atom false) first-left (atom nil) first-right (atom nil)
        finished (atom false) canceled (atom false)
        root (fn [state label]
               (fn [] (let [n @state]
                        [:button {:id "same" :label (str n) :hexpand true :vexpand true
                                  :on-click #(do (swap! calls conj [label n]) (swap! state inc))}])))]
    (qt/run-application!
     (fn []
       (check! (= 1 (register-fixture)) "Native QWidget factory registered")
       (reset! service (w/create! :test/widget {}))
       (check! (q/alive? @service) "Retained QWidget uses the same QApplication")
       (q/invoke! @service "show()" [])
       (check! (true? (q/read-property @service "visible")) "Retained QWidget can show safely")
       (q/invoke! @service "hide()" [])
       (let [temporary (w/create! :test/widget {})]
         (w/dispose! temporary)
         (check! (not (q/alive? temporary)) "Explicit QWidget disposal invalidates its handle"))
       (check! (zero? (qt/window-count)) "Application starts without an editor")
       (reset! a (qt/open-window! (root left :a) :title "Independent A"
                                 :on-close (fn [window]
                                             (check! (q/alive? window) "Close callback sees a live window")
                                             (swap! closes inc)
                                             @accept?)))
       (reset! b (qt/open-window! (root right :b) :title "Independent B"))
       (reset! first-left (qt/find-by-id @a "same"))
       (reset! first-right (qt/find-by-id @b "same"))
       (throws! #(w/find-by-id "same") #"Ambiguous")
       (check! (not= @first-left @first-right) "Lookup is scoped to each root")
       (check! @(future (try (qt/close-window! @a) false (catch Throwable _ true)))
               "Worker window mutations are rejected before changing ownership")
       (qt/activate! @a)
       (steps!
        [(fn []
           (check! (= 1 (click @first-left)) "Qt mouse input delivered to A")
           (qt/activate! @b))
         (fn []
           (check! (= [[:a 0]] @calls) "Mouse hit invokes only A's callback")
           (check! (= "1" (q/read-property @first-left "text")) "A state renders independently")
           (check! (= "10" (q/read-property @first-right "text")) "B remains unchanged")
           (q/set-property! @first-right "focus" true)
           (space @b))
         (fn []
           (check! (= [[:a 0] [:b 10]] @calls) "Keyboard input reaches focused B")
           (qt/reload-window! @a)
           (reset! left 5))
         (fn []
           (check! (not (q/alive? @first-left)) "Reload invalidates A's old tree")
           (check! (= @first-right (qt/find-by-id @b "same")) "B keeps its native identity")
           (check! (= [1 1] [(watches left) (watches right)]) "One watcher per independent root")
           (click (qt/find-by-id @a "same"))
           (check! (false? (q/invoke! @a "close()" [])) "Native close event is consumed")
           (qt/request-close! @a)
           (check! (zero? @closes) "Close policy never runs on native close-event stack"))
         (fn []
           (check! (= [:a 5] (last @calls)) "Reload installs a fresh callback")
           (check! (= 1 @closes) "Pending native close requests coalesce")
           (check! (qt/window-alive? @a) "Rejected close retains the window")
           (qt/later! @a 60000 #(reset! canceled true))
           (reset! accept? true)
           (qt/request-close! @a))
         (fn []
           (check! (not (qt/window-alive? @a)) "Accepted close disposes only A")
           (check! (zero? (watches left)) "Closed root detaches subscriptions")
           (check! (nil? (qt/later! @a 0 #(reset! canceled true))) "Closed window rejects owned jobs")
           (throws! #(qt/show! @a) #"disposed")
           (qt/minimize! @b))
         (fn []
           (check! (= (v/enum "Minimized") (q/read-property @b "visibility")) "Window is minimized")
           (qt/activate! @b))
         (fn []
           (check! (= (v/enum "Windowed") (q/read-property @b "visibility")) "Activation restores minimized window")
           (when (= "cocoa" (System/getenv "QT_QPA_PLATFORM"))
             (check! (true? (q/read-property @b "active")) "Desktop activation succeeds"))
           (qt/raise! @b)
           (qt/request-activate! @b)
           (click @first-right))
         (fn []
           (check! (= [:b 11] (last @calls)) "B remains interactive after A closes")
           (qt/hide! @b))
         (fn []
           (check! (false? (q/read-property @b "visible")) "Last editor hides without stopping application")
           (check! (q/alive? @service) "Retained QWidget survives hidden editors")
           (qt/show! @b)
           (qt/minimize! @b))
         (fn [] (qt/restore! @b))
         (fn []
           (check! (= (v/enum "Windowed") (q/read-property @b "visibility")) "Explicit restoration works")
           (qt/close-window! @b)
           (qt/close-window! @b)
           (check! (q/alive? @b) "Even explicit close defers native deletion"))
         (fn []
           (check! (zero? (qt/window-count)) "Application continues after last root closes")
           (check! (q/alive? @service) "QWidget service survives all editor closures")
           (check! (zero? (watches right)) "Last root subscriptions released")
           (reset! b (qt/open-window! (root right :b) :visible false)))
         (fn []
           (check! (false? (q/read-property @b "visible")) "New window can start hidden")
           (qt/show! @b)
           (qt/open-window! (root left :a))
           (check! (= 2 (qt/window-count)) "Explicit quit starts with two live roots")
           (check! (not @canceled) "Window-owned work canceled")
           (reset! finished true)
           (qt/quit!))]))
     :auto-quit-ms 8000)
    (check! @finished "Independent window scenario completed")
    (check! (not (qt/window-alive? @b)) "Explicit quit releases every window")
    (clean! [left right])))

(defn deferred-and-failed-close! []
  (let [cell (r/atom 0) window (atom nil) finished (atom false)]
    (qt/run-application!
     (fn []
        (reset! window
                (qt/open-window! (fn [] [:label {:label (str @cell)}])
                                 :on-close (fn [id]
                                             (qt/later! 100 #(do (reset! finished true) (qt/close-window! id)))
                                             nil)))
        (qt/request-close! @window))
     :keep-alive false :auto-quit-ms 2000)
    (check! @finished "Deferred callback can accept later and quit final window")
    (clean! [cell])
    (throws! #(qt/run-application!
               (fn []
                 (qt/open-window! (fn [] [:label {:label (str @cell)}]))
                 (let [id (qt/open-window! (fn [] [:label {:label (str @cell)}])
                                           :on-close (fn [_] (throw (ex-info "close failed" {}))))]
                   (qt/request-close! id)))
               :auto-quit-ms 2000)
             #"close failed")
    (clean! [cell])))

(defn failed-open-and-reload! []
  (let [cell (r/atom 0) passed (atom false)]
    (qt/run-application!
     (fn []
       (let [id (qt/open-window! (fn [] [:label {:label (str @cell)}]))]
         (throws! #(qt/open-window! (fn [] @cell [:unsupported])) #"Unsupported")
         (check! (= 1 (qt/window-count)) "Failed creation preserves existing root")
         (check! (= 1 (watches cell)) "Failed creation releases only failed subscriptions")
         (qt/close-window! id)
         (qt/reload-window! id)
         (qt/later! 100 #(do (check! (zero? (watches cell)) "Queued reload cannot revive closed root")
                            (reset! passed true) (qt/quit!)))))
     :auto-quit-ms 2000)
    (check! @passed "Closed reload scenario completed")
    (clean! [cell])
    (throws! #(qt/run-application!
               (fn []
                 (let [id (qt/open-window! (fn [] [:label {:label (str @cell)}]))]
                   (qt/open-window! (fn [] [:label {:label (str @cell)}]))
                   (qt/reload-window! id (fn [] @cell [:unsupported]))))
               :auto-quit-ms 2000)
             #"Unsupported")
    (clean! [cell])))

(defn compatibility-and-empty-start! []
  (let [cell (r/atom 0) other (atom nil) done (atom false)]
    (ui/run (fn [] [:label {:id "initial" :label (str @cell)}])
            :on-ready
            (fn []
              (reset! other (qt/open-window! (fn [] [:label {:id "other" :label (str @cell)}])))
              (ui/reload!)
              (steps!
               [(fn []
                  (check! (= 2 (watches cell)) "Convenience runner and independent root coexist")
                  (q/request-close @w/session))
                (fn []
                  (check! (= 1 (qt/window-count)) "Closing the initial root preserves the second editor")
                  (ui/reload!)
                  (reset! cell 7))
                (fn []
                  (check! (= "7" (q/read-property (qt/find-by-id @other "other") "text"))
                          "Second editor still renders after initial root closes")
                  (qt/close-window! @other)
                  (reset! done true))]))
            :auto-quit-ms 2000)
    (check! @done "Convenience runner quits on the final editor close")
    (clean! [cell]))
  (let [done (atom false)]
    (qt/run-application!
     (fn []
       (qt/later! 50
                  #(do (check! (zero? (qt/window-count)) "Event loop runs without any initial window")
                       (qt/open-window! (fn [] [:label]) :visible false)
                       (reset! done true)
                       (qt/quit!))))
     :auto-quit-ms 2000)
    (check! @done "Background startup can open a window later")
    (clean! [])))

(defn -main [& _]
  (try
    (independent-windows!)
    (deferred-and-failed-close!)
    (failed-open-and-reload!)
    (compatibility-and-empty-start!)
    (println "PASS:" @glimmer-qt.lifecycle-test/assertions "Qt independent-window checks")
    (finally (shutdown-agents))))
