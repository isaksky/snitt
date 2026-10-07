(ns glimmer-qt.layout-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.value :as v]
            [glimmer.ratom :as r]))

(def checks (atom 0))
(def done? (atom false))
(def order (r/atom ["Select" "Arrow" "Rectangle" "Text" "Undo" "Copy" "Save"]))
(def overlay? (r/atom true))
(def geometry-events (atom []))
(defn check! [yes message]
  (swap! checks inc)
  (when-not yes (throw (ex-info message {}))))
(defn id [name] (or (w/find-by-id name) (throw (ex-info "Missing item" {:id name}))))
(defn p [name prop] (q/read-property (id name) prop))
(defn near! [actual expected message]
  (check! (< (abs (- actual expected)) 0.02) (str message ": " actual " expected " expected)))
(defn rejects! [f]
  (check! (try (f) false (catch Throwable _ true)) "Conflicting geometry must fail"))
(defn root []
  [:vbox {:id "root" :hexpand true :vexpand true :padding 8 :padding-left 12 :spacing 6}
   [:flow {:id "toolbar" :hexpand true :spacing 5 :padding 3}
    (for [name @order] [:button {:key name :id name :label name}])]
   [:hbox {:id "sized" :hexpand true :preferred-height 45}
    [:rectangle {:id "fixed" :min-width 35 :preferred-width 50 :max-width 60
                 :min-height 20 :preferred-height 20 :max-height 20
                 :margin 2 :margin-left 7 :alignment (v/flags "AlignRight" "AlignBottom") :color "red"}]
    [:spacer {:id "spring" :hexpand true :implicit-width 10 :implicit-height 1}]]
   [:overlay {:id "canvas" :hexpand true :vexpand true :clip true
              :qt/subscriptions {:width {:property "width" :handler #(swap! geometry-events conj %)}}}
    [:rectangle {:id "background" :anchor "fill" :color "#223344"}]
    (when @overlay?
      [:rectangle {:key :annotation :id "annotation" :relative-x 0.25 :relative-y 0.125
                   :relative-width 0.5 :relative-height 0.25 :x 0.5 :y 0.25
                   :z 3 :opacity 0.5 :clip true :radius 2 :border-width 1 :border-color "white"}])
    [:item {:id "center" :anchor "center" :width 20.5 :height 10.25}
     [:label {:id "text" :label "Text overlay"}]]]])
(defn steps! [steps]
  (when-let [step (first steps)]
    (qt/later! 100 #(do (step) (steps! (rest steps))))))
(defn bounds! []
  (doseq [name @order]
    (check! (<= (+ (p name "x") (p name "width")) (- (p "toolbar" "width") 6))
            (str name " fits toolbar width"))
    (check! (<= (+ (p name "y") (p name "height")) (- (p "toolbar" "height") 6))
            (str name " fits toolbar height"))))
(defn geometry! []
  (near! (p "background" "width") (p "canvas" "width") "Fill width")
  (near! (p "annotation" "x") (+ 0.5 (* 0.25 (p "canvas" "width"))) "Fractional local x")
  (near! (p "annotation" "y") (+ 0.25 (* 0.125 (p "canvas" "height"))) "Fractional local y")
  (near! (p "annotation" "width") (* 0.5 (p "canvas" "width")) "Relative width")
  (near! (p "annotation" "height") (* 0.25 (p "canvas" "height")) "Relative height")
  (near! (p "center" "x") (/ (- (p "canvas" "width") 20.5) 2) "Center anchor")
  (check! (and (p "canvas" "clip") (p "annotation" "clip")) "Clipping")
  (near! (p "annotation" "opacity") 0.5 "Opacity")
  (near! (p "annotation" "z") 3 "Overlay stacking")
  (check! (= (v/object (id "annotation"))
             (q/invoke! (id "canvas") "childAt(double,double)"
                        [(+ 1.0 (p "annotation" "x")) (+ 1.0 (p "annotation" "y"))]))
          "Qt hit testing respects overlay stacking")
  (near! (q/read-property (q/child-target (id "root") "content") "x") 12 "Per-edge padding")
  (check! (<= 35 (p "fixed" "width") 60) "Layout size constraints")
  (near! (p "fixed" "height") 20 "Fixed preferred height")
  (near! (p "fixed" "x") 7 "Per-edge margin")
  (check! (> (p "spring" "width") 10) "Expanding spacer"))
(defn begin! [window]
  (let [arrow (id "Arrow") wide-height (p "toolbar" "height")]
    (bounds!) (geometry!)
    (check! (apply = (map #(p % "y") @order)) "Wide toolbar has one row")
    (doseq [[tag props] [[:item {:width 2 :relative-width 0.5}]
                        [:item {:anchor "sibling"}]
                        [:item {:anchor "fill" :x 1}]
                        [:rectangle {:min-width 80 :max-width 30}]
                        [:item {:width -2}] [:rectangle {:opacity 2}]]]
      (rejects! #(w/create! tag props)))
    (rejects! #(w/apply-props! :rectangle (id "fixed") {:x 2}))
    (rejects! #(w/apply-props! :button arrow {:preferred-width 40}))
    (q/set-property! window "width" 250)
    (steps!
     [(fn []
        (bounds!) (geometry!)
        (check! (> (p "toolbar" "height") wide-height) "Toolbar wraps at narrow width")
        (check! (> (count (distinct (map #(p % "y") @order))) 1) "Multiple visible rows")
        (reset! order ["Save" "Arrow" "Copy"]))
      (fn []
        (bounds!)
        (check! (= arrow (id "Arrow")) "Keyed identity survives reorder")
        (check! (nil? (w/find-by-id "Select")) "Removed control disposed")
        (check! (or (< (p "Save" "y") (p "Arrow" "y"))
                    (< (p "Save" "x") (p "Arrow" "x"))) "Flow visual order updated")
        (q/set-property! window "width" 900))
      (fn []
        (bounds!) (geometry!)
        (near! (p "toolbar" "height") wide-height "Wide height restored")
        (check! (< (p "Save" "x") (p "Arrow" "x") (p "Copy" "x")) "Wide keyed order")
        (check! (> (count @geometry-events) 1) "Geometry observers report resize")
        (w/apply-props! :rectangle (id "annotation")
                        {:id "annotation" :implicit-width 33.5 :implicit-height 17.25})
        (w/apply-props! :rectangle (id "fixed")
                        {:id "fixed" :preferred-width 40 :preferred-height 20 :margin 4}))
      (fn []
        (near! (p "annotation" "x") 0 "Removed position resets")
        (near! (p "annotation" "width") 33.5 "Removed relative sizing restores implicit width")
        (near! (p "annotation" "opacity") 1 "Removed opacity resets")
        (near! (p "fixed" "x") 4 "Removed edge margin inherits aggregate")
        (near! (p "fixed" "Layout.maximumWidth") 16777215 "Removed maximum resets")
        (reset! overlay? false))
      (fn []
        (check! (nil? (w/find-by-id "annotation")) "Overlay removal")
        (near! (p "background" "width") (p "canvas" "width") "Other overlays survive")
        (reset! done? true)
        (qt/quit!))])))
(defn -main [& _]
  (try
    (qt/run-application!
     #(let [window (qt/open-window! root :width 900 :height 600 :title "Layout verification")]
        (qt/later! 150 (fn [] (begin! window))))
     :auto-quit-ms 6000)
    (check! @done? "Layout test completed")
    (check! (zero? (w/live-count)) "All geometry owners released")
    (println "PASS:" @checks "layout checks")
    (finally (shutdown-agents))))
