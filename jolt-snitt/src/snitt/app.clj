(ns snitt.app
  "Jolt-owned editor window; EditorCanvas remains the document owner."
  (:require [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [snitt.native :as native]))

(defonce state (r/atom {:tool "rect" :status "Open or paste an image"}))
(defonce runtime (atom nil))

(defn canvas [] (widget/find-by-id "snitt-canvas"))

(defn- report! [message] (swap! state assoc :status message))

(defn- refresh! []
  (when-let [handle (canvas)]
    (let [has-image? (q/read-property handle "hasImage")]
      (report! (if has-image?
                 (str (q/read-property handle "imageWidth") " × "
                      (q/read-property handle "imageHeight"))
                 "Open or paste an image")))))

(defn open-url! [url]
  (if (native/load-url! (canvas) (value/url url))
    (refresh!)
    (report! "Could not open image")))

(defn- command! [action success-message]
  (let [handle (canvas)]
    (if-let [result (action handle)]
      (report! (if (string? result) result success-message))
      (refresh!))))

(defn editor []
  (let [{:keys [tool status]} @state]
    [:vbox {:hexpand true :vexpand true :padding 10 :spacing 8}
     [:hbox {:spacing 6}
      [:button {:id "open" :label "Open" :on-click #(widget/open-dialog! (widget/find-by-id "open-file"))}]
      [:button {:id "paste" :label "Paste" :on-click #(command! (fn [c] (object/invoke! c "paste()" [])) "Pasted")}]
      [:button {:id "rect" :label "Rectangle" :checkable true :checked (= tool "rect")
                :on-click #(swap! state assoc :tool "rect")}]
      [:button {:id "arrow" :label "Arrow" :checkable true :checked (= tool "arrow")
                :on-click #(swap! state assoc :tool "arrow")}]
      [:button {:id "undo" :label "Undo" :on-click #(do (native/undo! (canvas)) (refresh!))}]
      [:button {:id "redo" :label "Redo" :on-click #(do (native/redo! (canvas)) (refresh!))}]
      [:button {:id "copy" :label "Copy" :on-click #(command! native/copy! "Copied")}]
      [:button {:id "save" :label "Save" :on-click #(command! native/save! "Saved")}]]
     [:pointer-area {:id "snitt-pointer" :hexpand true :vexpand true
                     :preferred-width 800 :preferred-height 500
                     :buttons (value/flags "LeftButton")
                     :on-pointer-press (fn [{:keys [x y]}] (native/begin! (canvas) x y) true)
                     :on-pointer-move (fn [{:keys [x y]}] (native/move! (canvas) x y))
                     :on-pointer-release (fn [{:keys [x y]}] (native/end! (canvas) x y) (refresh!))
                     :on-pointer-cancel (fn [_] (native/cancel! (canvas)))}
      [:snitt/canvas {:id "snitt-canvas" :width 800 :height 500 :tool tool
                      :qt/subscriptions
                      {:image {:signal "imageChanged()" :handler refresh!}
                       :error {:signal "error(QString)" :handler report!}}}]]
     [:label {:id "status" :label status}]
     [:file-chooser {:id "open-file" :on-accepted open-url!}]]))

(defn start! [& {:keys [image]}]
  (native/install!)
  (let [services (native/create-services!)]
    (try
      (let [window (qt/open-window! editor :title "Snitt · Jolt development"
                                    :width 900 :height 640
                                    :on-close (fn [_]
                                                (when (q/alive? (:root services))
                                                  (widget/dispose! (:root services)))
                                                true))]
        (reset! runtime {:window window :services services})
        (q/set-property! (canvas) "saveRoot"
                         (q/read-property (:settings services) "picturesRoot"))
        (when image
          (when-not (native/load-image! (canvas) image)
            (report! (str "Could not open " image))))
        (refresh!)
        @runtime)
      (catch Throwable error
        (widget/dispose! (:root services))
        (throw error)))))

(defn -main [& args]
  (let [smoke? (= args ["--smoke-test"])
        image (when (and (= 1 (count args)) (not smoke?)) (first args))]
    (when (and (seq args) (not smoke?) (nil? image))
      (throw (ex-info "Usage: run [image-path | --smoke-test]" {:args args})))
    (try
      (qt/run-application!
       #(do (start! :image image)
            (when smoke?
              (qt/later! 250
                (fn []
                  (when-not (and (qt/window-alive? (:window @runtime))
                                 (q/alive? (canvas))
                                 (q/alive? (get-in @runtime [:services :backend])))
                    (throw (ex-info "Snitt Jolt smoke failed" {})))
                  (qt/quit!)))))
       :app-id "snitt-jolt-development" :keep-alive false
       :auto-quit-ms (when smoke? 5000))
      (finally (reset! runtime nil) (shutdown-agents)))))
