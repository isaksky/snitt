(ns hello.smoke
  "Integration checks against real Qt Quick controls and the running event loop."
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]
            [hello.core :as hello]))

(def rows (r/atom [:a :b :c]))
(def leaf-tag (r/atom :label))
(def show-fixture? (r/atom true))
(def passed? (atom false))
(def assertions (atom 0))

(defn check! [truth message]
  (swap! assertions inc)
  (when-not truth (throw (ex-info message {}))))

(defn widget [id]
  (or (w/find-by-id id) (throw (ex-info "Missing Qt control." {:id id}))))

(defn property [id prop] (q/read-property (widget id) prop))

(defn signal! [id signal]
  (check! (= 1 (q/emit-signal (widget id) signal)) (str "Could not emit " signal)))

(defn edit! [text]
  (q/set-string (widget "name") "text" text)
  (signal! "name" "textEdited"))

(defn fixture []
  [:vbox {:id "fixture" :hexpand true}
   [:hbox {:id "rows" :hexpand true}
    (for [key @rows]
      [:label {:key key :id (name key) :label (name key)}])]
   [@leaf-tag {:id "changing-leaf" :label "replacement"}]])

(defn root []
  [:vbox {:hexpand true :vexpand true}
   [hello/app]
   (when @show-fixture? [fixture])])

(defn steps! [steps]
  (when-let [step (first steps)]
    (qt/later! 80 (fn [] (step) (steps! (rest steps))))))

(defn begin! []
  (let [entry (widget "name")
        label (widget "greeting")
        row-b (widget "b")]
    (check! (= "Hello, world!" (property "greeting" "text")) "Initial greeting")
    (check! (pos? (property "name" "width")) "Entry has a layout width")
    ;; Each step lets the queued reactive render run before the next inspection.
    (steps!
      [#(edit! "  Jolt 🌍  ")
       (fn []
         (check! (= "  Jolt 🌍  " (:draft @hello/state)) "Unicode input callback")
         (check! (= entry (widget "name")) "Entry must survive re-render")
         (signal! "hello" "clicked"))
       (fn []
         (check! (= "Hello, Jolt 🌍!\nClick 1 handled by Jolt + Glimmer."
                    (property "greeting" "text")) "Reactive label and fresh handler closure")
         (check! (= label (widget "greeting")) "Label must update in place")
         (edit! "   "))
       #(signal! "name" "accepted")
       (fn []
         (check! (= "Hello, world!\nClick 2 handled by Jolt + Glimmer."
                    (property "greeting" "text")) "Enter key and blank fallback")
         (reset! rows [:c :b :d])
         (reset! leaf-tag :button))
       (fn []
         (check! (= row-b (widget "b")) "Keyed child must be reused")
         (check! (nil? (w/find-by-id "a")) "Removed keyed child must be disposed")
         (check! (< (property "c" "x")
                    (property "b" "x")
                    (property "d" "x")) "Keyed visual order")
         (signal! "changing-leaf" "clicked")
         (reset! show-fixture? false)
         ;; Updating a ratom on a worker must post to Qt's main loop.
         (future (swap! hello/state assoc :name "worker ✓" :draft "worker")))
       (fn []
         (check! (nil? (w/find-by-id "b")) "Container subtree cleanup")
         (check! (= "Hello, worker ✓!\nClick 2 handled by Jolt + Glimmer."
                    (property "greeting" "text")) "Worker-triggered render")
         (check! (= "worker" (property "name" "text")) "Controlled entry update")
         (check! (= 2 (:clicks @hello/state)) "Prop writes must not emit user events")
         (reset! passed? true)
         (qt/quit!))])))

(defn -main [& args]
  (when (seq args) (throw (ex-info "The smoke test takes no arguments." {})))
  (try
    (ui/run root :title "Glimmer Qt smoke test" :width 560 :height 480
            :on-ready #(qt/later! 80 begin!) :auto-quit-ms 5000)
    (check! @passed? "Smoke test timed out before finishing")
    (check! (nil? (w/find-by-id "name")) "Session cleanup")
    ;; Disposed reactive watchers must not try to touch deleted native pointers.
    (swap! hello/state assoc :name "after shutdown")
    (println "PASS:" @assertions "checks — Qt signals, reactive updates, Unicode,"
             "widget reuse/reconciliation, worker scheduling, and shutdown.")
    (finally (shutdown-agents))))
