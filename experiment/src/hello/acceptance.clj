(ns hello.acceptance
  "Snitt-shaped binding fixture. State and command policy live in the application,
  while Glimmer owns the rendered trees and glimmer-qt owns their Qt lifetime."
  (:require [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.media :as media]
            [glimmer-qt.tray :as tray]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [hello.snitt :as snitt]))

(defonce editors (atom {}))
(defonce review (r/atom {:playing? false :position 0.0 :start 0.0 :end 3.0
                         :progress 0.0 :busy? false :error "" :canceled? false}))
(defonce events (atom []))
(defonce handles (atom {}))

(defn editor-state [name]
  (get @editors name))

(defn editor-root [name image]
  (let [state (editor-state name)
        prefix (clojure.core/name name)]
    (fn []
      [:vbox {:id (str prefix "-root") :hexpand true :vexpand true
              :padding 12 :spacing 8}
       [:label {:label (str "Snitt · " prefix) :font-size 20 :font-family "Arial"}]
       [:flow {:id (str prefix "-toolbar") :hexpand true :spacing 4}
        (for [tool ["rect" "cut" "text" "arrow"]]
          [:button {:key tool :id (str prefix "-" tool) :label tool :font-family "Arial"
                    :checked (= tool (:tool @state)) :checkable true
                    :on-click #(do (swap! events conj [name :tool tool])
                                   (swap! state assoc :tool tool))}])
        [:button {:id (str prefix "-open") :label "Open image" :font-family "Arial"
                  :on-click #(widget/open-dialog! (widget/find-by-id (str prefix "-file")))}]
        [:button {:id (str prefix "-arrange") :label "Arrange" :font-family "Arial"
                  :on-click #(widget/open-dialog! (widget/find-by-id (str prefix "-confirm")))}]]
       [:overlay {:id (str prefix "-stage") :hexpand true :vexpand true}
        [:pointer-area {:id (str prefix "-pointer") :width 320 :height 210
                        :on-pointer-press #(do (swap! events conj [name :pointer (:x %)]) true)
                        :on-pointer-release (fn [_] (swap! events conj [name :release]) true)}
         [:snitt/canvas {:id (str prefix "-canvas") :width 320 :height 210
                         :tool (:tool @state)}]]]
       [:scroll {:hexpand true :preferred-height 95}
        [:text-area {:id (str prefix "-annotation") :text (:annotation @state)
                     :font-family "Arial"
                     :on-change #(do (swap! events conj [name :annotation %])
                                    (swap! state assoc :annotation %))}]]
       [:label {:id (str prefix "-status") :font-family "Arial"
                :label (str "Tool: " (:tool @state) " · " (:status @state))}]
       [:shortcut {:id (str prefix "-shortcut") :sequences ["Ctrl+K"]
                   :on-activate #(do (swap! events conj [name :shortcut])
                                    (swap! state assoc :tool "cut"))}]
       [:dialog {:id (str prefix "-confirm") :title "Discard annotations?"
                 :on-accepted #(do (swap! events conj [name :confirm])
                                   (swap! state assoc :annotation ""))
                 :on-rejected #(swap! events conj [name :cancel])}
        [:label {:label "Return to arrangement and discard annotations?" :font-family "Arial"}]
        [:hbox {}
         [:button {:id (str prefix "-reject") :label "Cancel" :font-family "Arial"
                   :on-click #(widget/reject-dialog! (widget/find-by-id (str prefix "-confirm")))}]
         [:button {:id (str prefix "-accept") :label "Arrange" :font-family "Arial"
                   :on-click #(widget/accept-dialog! (widget/find-by-id (str prefix "-confirm")))}]]]
       [:file-chooser {:id (str prefix "-file")
                       :selected-url (value/local-file image)
                       :on-accepted #(swap! events conj [name :file %])
                       :on-rejected #(swap! events conj [name :file-cancel])}]])))

(defn review-root [image]
  (fn []
    [:vbox {:id "review-root" :hexpand true :vexpand true :padding 12 :spacing 8}
     [:label {:label "Recording review" :font-size 20 :font-family "Arial"}]
     [:video-player {:id "review-video" :hexpand true :vexpand true
                     :on-first-frame #(swap! events conj [:review :first-frame])}]
     [:hbox {:id "filmstrip" :hexpand true :preferred-height 68 :spacing 2}
      (for [index (range 4)]
        [:image {:key index :id (str "thumb-" index) :hexpand true
                 :preferred-height 64 :source (value/local-file image)
                 :fill-mode "crop"}])]
     [:hbox {:hexpand true :spacing 8}
      [:slider {:id "trim-start" :hexpand true :from 0 :to 3
                :value (:start @review) :accessible-name "Trim start"
                :on-change #(swap! review assoc :start %)}]
      [:slider {:id "seek" :hexpand true :from 0 :to 3
                :value (:position @review) :accessible-name "Playback position"
                :on-change #(do (swap! review assoc :position %)
                                (media/position! (widget/find-by-id "review-video")
                                                 (long (* 1000 %))))}]
      [:slider {:id "trim-end" :hexpand true :from 0 :to 3
                :value (:end @review) :accessible-name "Trim end"
                :on-change #(swap! review assoc :end %)}]]
     [:hbox {:hexpand true :spacing 6}
      [:button {:id "play" :label (if (:playing? @review) "Pause" "Play")
                :font-family "Arial"
                :on-click #(let [surface (widget/find-by-id "review-video")]
                             (if (:playing? @review) (media/pause! surface) (media/play! surface))
                             (swap! review update :playing? not))}]
      [:button {:id "cancel-export" :label "Cancel export" :font-family "Arial"
                :on-click #(swap! review assoc :busy? false :canceled? true :progress 0.0)}]
      [:label {:id "review-error" :label (:error @review) :font-family "Arial"}]]
     [:progress {:id "export-progress" :hexpand true :from 0 :to 1
                 :value (:progress @review) :indeterminate (:busy? @review)}]]))

(defn overlay-root []
  [:overlay {:hexpand true :vexpand true}
   [:rectangle {:relative-width 1 :relative-height 1 :color "#55000000"}]
   [:label {:label "Select capture region" :font-size 20 :font-family "Arial"}]])

(defn start! [directory image]
  (snitt/install! directory)
  (doseq [name [:left :right]]
    (swap! editors assoc name (r/atom {:tool "rect" :annotation "" :status "Ready"})))
  (let [left (qt/open-window! (editor-root :left image) :title "Snitt editor A"
                               :width 600 :height 440)
        right (qt/open-window! (editor-root :right image) :title "Snitt editor B"
                                :width 520 :height 420)
        review-window (qt/open-window! (review-root image) :title "Snitt review"
                                       :width 620 :height 480)
        service (widget/create! :snitt/services {:id "acceptance-services"})
        selector (widget/create! :snitt/selector {:id "acceptance-selector"})
        icon (str (System/getProperty "user.dir") "/../src/icons/scissors.svg")
        tray-handle (tray/create! icon :tooltip "Snitt acceptance")]
    (let [open-action (tray/add-action! tray-handle :label "Open editor"
                                        :on-trigger #(do
                                                       (swap! events conj [:tray :open])
                                                       (if (qt/window-alive? (:left @handles))
                                                         (qt/activate! (:left @handles))
                                                         (swap! handles assoc :left
                                                                (qt/open-window!
                                                                 (editor-root :left image)
                                                                 :title "Snitt editor reopened"
                                                                 :width 600 :height 440)))))
          quit-action (tray/add-action! tray-handle :label "Quit"
                                        :on-trigger #(do (swap! events conj [:tray :quit])
                                                         (qt/quit!)))]
      (swap! handles assoc :tray-open open-action :tray-quit quit-action))
    (when (tray/available?) (tray/show! tray-handle))
    (swap! handles merge {:left left :right right :review review-window
                          :service service :selector selector :tray tray-handle})
    @handles))
