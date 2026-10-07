(ns hello.core
  (:require [clojure.string :as str]
            [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core]))

(defonce state (r/atom {:name "world" :draft "world" :clicks 0}))

(defn greeting [{:keys [name clicks]}]
  (str "Hello, " name "!"
       (when (pos? clicks)
         (str "\nClick " clicks " handled by Jolt + Glimmer."))))

(defn app []
  (let [{:keys [draft] :as value} @state
        say-hello #(swap! state
                         (fn [s] (-> s
                                     (assoc :name (if (str/blank? draft)
                                                    "world" (str/trim draft)))
                                     (update :clicks inc))))]
    [:vbox {:spacing 16 :margin 36 :hexpand true :vexpand true}
     [:label {:label "SNITT / JOLT + GLIMMER"
              :color "#91bff0" :font-size 12}]
     [:label {:id "greeting" :label (greeting value)
              :font-size 27 :font-weight 600 :hexpand true :vexpand true}]
     [:label {:label "Qt Quick controls, rendered by Glimmer."
              :color "#aeb6c2" :hexpand true}]
     [:hbox {:spacing 12 :hexpand true}
      [:entry {:id "name" :text draft :placeholder "Your name" :hexpand true
               :on-change #(swap! state assoc :draft %)
               :on-activate say-hello}]
      [:button {:id "hello" :label "Say hello" :highlighted true
                :on-click say-hello}]]]))

(defn -main [& args]
  (when (seq args)
    (throw (ex-info "Usage: ./experiment/run [--smoke-test]" {:args args})))
  (ui/run app :title "Snitt · Jolt + Glimmer" :width 560 :height 360
          :app-id "snitt.glimmer-experiment"))
