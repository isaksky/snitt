(ns glimmer.lifecycle-test
  (:require [clojure.test :refer [deftest is use-fixtures]]
            [glimmer.backend :as b]
            [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer.mock-backend :as mock]))

(use-fixtures :each
  (fn [test]
    (mock/install!)
    (reset! b/loop-running? false)
    (try (test) (finally (reset! b/loop-running? false)))))

(deftest explicit-lifecycle-follows-reload-without-a-mount-return-value
  (let [cell (r/atom 0) win (mock/widget :window {})]
    (b/register!
     (assoc mock/backend :run-lifecycle
            (fn [_ {:keys [mount! unmount!]}]
              (is (nil? (mount! win :window)))
              (dotimes [_ 8]
                (ui/reload!)
                (is (= 1 (count @(:watches cell))))
                (is (= 1 (count (mock/children win)))))
              (unmount!)
              (unmount!)
              (is (empty? @(:watches cell)))
              (is (empty? (mock/children win))))))
    (ui/run (fn [] [:label {:label @cell}]))
    (reset! cell 1)
    (ui/reload!)
    (is (empty? (mock/children win)))))

(deftest partial-mount-detaches-new-child-subscriptions
  (let [cell (r/atom 0) win (mock/widget :window {})
        bad (fn [] @cell (throw (ex-info "render failed" {})))]
    (is (thrown? Exception (ui/mount win :window [:vbox [bad]])))
    (is (empty? @(:watches cell)))
    (is (empty? (mock/children win)))))

(deftest conditional-subscriptions-and-nil-components-dispose-immediately
  (let [show (r/atom true) cell (r/atom "value") win (mock/widget :window {})
        root (ui/mount win :window [(fn [] (when @show [:label {:label @cell}]))])]
    (is (= 1 (count @(:watches cell))))
    (reset! show false)
    (is (empty? @(:watches cell)))
    (is (empty? (mock/children win)))
    (ui/unmount! root win :window)
    (is (empty? @(:watches show)))))

(deftest replacing-form-two-components-discards-state-and-subscriptions
  (let [cell (r/atom 0) which (r/atom true) mounts (atom [])
        a (fn [] (swap! mounts conj :a) (fn [] [:label {:label @cell}]))
        z (fn [] (swap! mounts conj :z) (fn [] [:label {:label "z"}]))
        win (mock/widget :window {})
        root (ui/mount win :window [(fn [] [(if @which a z)])])]
    (is (= [:a] @mounts))
    (reset! which false)
    (is (= [:a :z] @mounts))
    (is (empty? @(:watches cell)))
    (is (= "z" (get-in @(first (mock/children win)) [:props :label])))
    (ui/unmount! root win :window)
    (is (empty? @(:watches which)))))

(deftest queued-renders-and-reloads-are-fenced-by-their-owner
  (let [queue (atom []) renders (atom 0) disposed (atom false)
        watcher (ui/make-rerender-watcher #(swap! renders inc) (atom true)
                                          #(swap! queue conj %) disposed)]
    (watcher nil)
    (reset! disposed true)
    ((first @queue))
    (is (zero? @renders)))
  (let [queue (atom []) win (mock/widget :window {}) phase (atom 0)]
    (b/register!
     (assoc mock/backend :schedule #(swap! queue conj %)
            :run-lifecycle
            (fn [_ {:keys [mount! unmount!]}]
              (reset! b/loop-running? true)
              (mount! win :window)
              (if (zero? @phase)
                (do (ui/reload! (fn [] [:label {:label "stale"}])) (swap! phase inc))
                (do ((first @queue))
                    (is (= "current" (get-in @(first (mock/children win)) [:props :label])))))
              (unmount!))))
    (ui/run (fn [] [:label {:label "first"}]))
    (ui/run (fn [] [:label {:label "current"}]))))

(deftest concurrent-runs-cannot-overwrite-the-current-root
  (let [start (promise) release (promise) denied (promise)
        attempts (atom 0) admitted (atom 0)]
    (b/register!
     (assoc mock/backend :run-lifecycle
            (fn [_ _]
              (swap! admitted inc)
              @release)))
    (let [runs (doall
                (for [_ (range 8)]
                  (future
                    @start
                    (try
                      (ui/run (fn [] [:label]))
                      (catch Exception _
                        (when (= 7 (swap! attempts inc)) (deliver denied true)))))))]
      (deliver start true)
      (try
        (is (= true (deref denied 5000 :timeout)))
        (is (= 1 @admitted))
        (finally
          (deliver release true)
          (doseq [run runs] @run))))))
