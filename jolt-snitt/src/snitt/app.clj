(ns snitt.app
  "Jolt-owned editor window; EditorCanvas remains the document owner."
  (:require [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [snitt.native :as native]))

;; Only presentation and command availability live here. Image pixels and history
;; stay in EditorCanvas; imageChanged is the source of every toolbar update.
(defonce state (r/atom {:tool "rect" :status "Open or paste an image"
                        :error? false :has-image? false :can-undo? false :can-redo? false
                        :canvas-width 800 :canvas-height 500}))
(defonce runtime (atom nil))

(defn canvas [] (widget/find-by-id "snitt-canvas"))

(defn- report! [message error?]
  (swap! state assoc :status message :error? error?))

(defn- refresh! []
  (when-let [handle (canvas)]
    (let [has-image? (q/read-property handle "hasImage")]
      (swap! state assoc
             :has-image? has-image?
             :can-undo? (q/read-property handle "canUndo")
             :can-redo? (q/read-property handle "canRedo")
             :status (if has-image?
                       (str (q/read-property handle "imageWidth") " × "
                            (q/read-property handle "imageHeight"))
                       "Open or paste an image")
             :error? false))))

(defn- sync-canvas-size! []
  (when-let [area (widget/find-by-id "snitt-pointer")]
    (let [width (q/read-property area "width")
          height (q/read-property area "height")]
      (when (and (pos? width) (pos? height)
                 (not= [width height]
                       ((juxt :canvas-width :canvas-height) @state)))
        (swap! state assoc :canvas-width width :canvas-height height)))))

(defn open-url! [url]
  (when-not (native/load-url! (canvas) (value/url url))
    ;; The native error signal supplies the reader's detail on the next turn.
    (report! "Could not open this image. Choose a supported image file and try again." true)))

(defn- command! [action success-message failure-message]
  (let [result (action (canvas))]
    (if (and result (not (and (string? result) (empty? result))))
      (report! (if (string? result) result success-message) false)
      (report! failure-message true))))

(defn- choose-tool! [tool] (swap! state assoc :tool tool))
(defn- undo! [] (native/undo! (canvas)))
(defn- redo! [] (native/redo! (canvas)))
(defn- copy! [] (command! native/copy! "Copied to clipboard"
                         "Could not copy to the clipboard. Try again."))
(defn- save! [] (command! native/save! "Saved"
                         "Could not save the image. Check the Pictures folder and try again."))
(defn- open-dialog! [] (widget/open-dialog! (widget/find-by-id "open-file")))

(defn editor []
  (let [{:keys [tool status error? has-image? can-undo? can-redo?
                canvas-width canvas-height]} @state]
    [:vbox {:hexpand true :vexpand true :padding 10 :spacing 8}
     [:hbox {:spacing 6}
      [:button {:id "open" :label "Open" :on-click open-dialog!}]
      [:button {:id "paste" :label "Paste"
                :on-click #(command! (fn [c] (object/invoke! c "paste()" [])) "Pasted"
                                     "The clipboard has no image to paste.")}]
      [:button {:id "rect" :label "Rectangle" :checkable true :checked (= tool "rect")
                :sensitive has-image? :on-click #(choose-tool! "rect")}]
      [:button {:id "arrow" :label "Arrow" :checkable true :checked (= tool "arrow")
                :sensitive has-image? :on-click #(choose-tool! "arrow")}]
      [:button {:id "undo" :label "Undo" :sensitive can-undo? :on-click undo!}]
      [:button {:id "redo" :label "Redo" :sensitive can-redo? :on-click redo!}]
      [:button {:id "copy" :label "Copy" :sensitive has-image? :on-click copy!}]
      [:button {:id "save" :label "Save" :sensitive has-image? :on-click save!}]]
     [:pointer-area {:id "snitt-pointer" :hexpand true :vexpand true
                     :preferred-width 800 :preferred-height 500
                     :buttons (value/flags "LeftButton") :sensitive has-image?
                     :qt/subscriptions
                     {:width {:signal "widthChanged()" :handler sync-canvas-size!}
                      :height {:signal "heightChanged()" :handler sync-canvas-size!}}
                     :on-pointer-press (fn [{:keys [x y]}] (native/begin! (canvas) x y) true)
                     :on-pointer-move (fn [{:keys [x y]}] (native/move! (canvas) x y))
                     :on-pointer-release (fn [{:keys [x y]}] (native/end! (canvas) x y))
                     :on-pointer-cancel (fn [_] (native/cancel! (canvas)))}
      [:snitt/canvas {:id "snitt-canvas" :width canvas-width :height canvas-height :tool tool
                      :qt/subscriptions
                      {:image {:signal "imageChanged()" :handler refresh!}
                       :error {:signal "error(QString)" :handler #(report! % true)}}}]]
     [:label {:id "status" :label status :color (if error? "#ffb4a8" "#ffffff")}]
     [:file-chooser {:id "open-file" :on-accepted open-url!}]
     [:shortcut {:id "open-key" :sequences ["Ctrl+O"] :on-activate open-dialog!}]
     [:shortcut {:id "rect-key" :sequences ["R"] :enabled has-image?
                 :on-activate #(choose-tool! "rect")}]
     [:shortcut {:id "undo-key" :sequences ["Ctrl+Z"] :enabled can-undo?
                 :on-activate undo!}]
     [:shortcut {:id "redo-key" :sequences ["Ctrl+Shift+Z"] :enabled can-redo?
                 :on-activate redo!}]
     [:shortcut {:id "copy-key" :sequences ["Ctrl+C"] :enabled has-image?
                 :on-activate copy!}]
     [:shortcut {:id "save-key" :sequences ["Ctrl+S"] :enabled has-image?
                 :on-activate save!}]]))

(defn start! [& {:keys [image]}]
  (native/install!)
  (reset! state {:tool "rect" :status "Open or paste an image"
                 :error? false :has-image? false :can-undo? false :can-redo? false
                 :canvas-width 800 :canvas-height 500})
  (let [services (native/create-services!)]
    (try
      (let [window (qt/open-window! editor :title "Snitt · Jolt development"
                                    :width 900 :height 640
                                    :on-close (fn [_]
                                                (when (q/alive? (:root services))
                                                  (widget/dispose! (:root services)))
                                                true))]
        (reset! runtime {:window window :services services})
        (sync-canvas-size!)
        (q/set-property! (canvas) "saveRoot"
                         (q/read-property (:settings services) "picturesRoot"))
        (refresh!)
        (when image
          (when-not (native/load-image! (canvas) image)
            (report! (str "Could not open " image) true)))
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
