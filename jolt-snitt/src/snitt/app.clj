(ns snitt.app
  "Jolt-owned editor sessions; each EditorCanvas remains its document owner."
  (:require [glimmer.ratom :as r]
            [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [snitt.native :as native]
            [snitt.sessions :as sessions]))

(defn- initial-state []
  {:tool "rect" :status "Open or paste an image" :error? false
   :has-image? false :can-undo? false :can-redo? false
   :canvas-width 800 :canvas-height 500})

;; The first session remains addressable for the development smoke and parity
;; tests; every additional editor gets its own atom and independent Glimmer root.
(defonce state (r/atom (initial-state)))
(defonce runtime (atom nil))

(defn canvas
  ([] (when-let [window (:window @runtime)] (qt/find-by-id window "snitt-canvas")))
  ([context] (when-let [window @(:window context)]
               (when (qt/window-alive? window)
                 (qt/find-by-id window "snitt-canvas")))))

(defn- item [context id]
  (when-let [window @(:window context)]
    (when (qt/window-alive? window) (qt/find-by-id window id))))

(defn- report! [context message error?]
  (swap! (:state context) assoc :status message :error? error?))

(defn- refresh! [context]
  (when-let [handle (canvas context)]
    (let [has-image? (q/read-property handle "hasImage")]
      (swap! (:state context) assoc
             :has-image? has-image?
             :can-undo? (q/read-property handle "canUndo")
             :can-redo? (q/read-property handle "canRedo")
             :status (if has-image?
                       (str (q/read-property handle "imageWidth") " × "
                            (q/read-property handle "imageHeight"))
                       "Open or paste an image")
             :error? false))))

(defn- sync-canvas-size! [context]
  (when-let [area (item context "snitt-pointer")]
    (let [width (q/read-property area "width")
          height (q/read-property area "height")]
      (when (and (pos? width) (pos? height)
                 (not= [width height]
                       ((juxt :canvas-width :canvas-height) @(:state context))))
        (swap! (:state context) assoc :canvas-width width :canvas-height height)))))

(defn- open-url! [context url]
  (if (native/load-url! (canvas context) (value/url url))
    true
    (do (report! context "Could not open this image. Choose a supported image file and try again." true)
        false)))

(defn- command! [context action success-message failure-message]
  (let [result (action (canvas context))]
    (if (and result (not (and (string? result) (empty? result))))
      (report! context (if (string? result) result success-message) false)
      (report! context failure-message true))))

(defn- choose-tool! [context tool] (swap! (:state context) assoc :tool tool))
(defn- open-dialog! [context] (widget/open-dialog! (item context "open-file")))

(defn- accept-image! [context url]
  (if (:has-image? @(:state context))
    (let [path (q/local-file-path (value/url url))
          label (last (str/split path #"[/\\]"))]
      (try (sessions/open-image! (:manager @runtime) url label)
           (catch Throwable _
             (report! context "Could not open this image. Choose a supported image file and try again." true))))
    (open-url! context url)))

(defn editor [context]
  (let [{:keys [tool status error? has-image? can-undo? can-redo?
                canvas-width canvas-height]} @(:state context)
        manager (:manager @runtime)]
    [:vbox {:hexpand true :vexpand true :padding 10 :spacing 8}
     [:hbox {:spacing 6}
      [:button {:id "open" :label "Open" :on-click #(open-dialog! context)}]
      [:button {:id "paste" :label "Paste"
                :on-click #(command! context (fn [c] (object/invoke! c "paste()" [])) "Pasted"
                                     "The clipboard has no image to paste.")}]
      [:button {:id "capture" :label "Capture"
                :on-click #(sessions/capture! manager false false)}]
      [:button {:id "record" :label "Record"
                :on-click #(sessions/capture! manager false true)}]
      [:button {:id "rect" :label "Rectangle" :checkable true :checked (= tool "rect")
                :sensitive has-image? :on-click #(choose-tool! context "rect")}]
      [:button {:id "arrow" :label "Arrow" :checkable true :checked (= tool "arrow")
                :sensitive has-image? :on-click #(choose-tool! context "arrow")}]
      [:button {:id "undo" :label "Undo" :sensitive can-undo?
                :on-click #(native/undo! (canvas context))}]
      [:button {:id "redo" :label "Redo" :sensitive can-redo?
                :on-click #(native/redo! (canvas context))}]
      [:button {:id "copy" :label "Copy" :sensitive has-image?
                :on-click #(command! context native/copy! "Copied to clipboard"
                                     "Could not copy to the clipboard. Try again.")}]
      [:button {:id "save" :label "Save" :sensitive has-image?
                :on-click #(command! context native/save! "Saved"
                                     "Could not save the image. Check the Pictures folder and try again.")}]]
     [:pointer-area {:id "snitt-pointer" :hexpand true :vexpand true
                     :preferred-width 800 :preferred-height 500
                     :buttons (value/flags "LeftButton") :sensitive has-image?
                     :qt/subscriptions
                     {:width {:signal "widthChanged()" :handler #(sync-canvas-size! context)}
                      :height {:signal "heightChanged()" :handler #(sync-canvas-size! context)}}
                     :on-pointer-press (fn [{:keys [x y]}] (native/begin! (canvas context) x y) true)
                     :on-pointer-move (fn [{:keys [x y]}] (native/move! (canvas context) x y))
                     :on-pointer-release (fn [{:keys [x y]}] (native/end! (canvas context) x y))
                     :on-pointer-cancel (fn [_] (native/cancel! (canvas context)))}
      [:snitt/canvas {:id "snitt-canvas" :width canvas-width :height canvas-height :tool tool
                      :qt/subscriptions
                      {:image {:signal "imageChanged()" :handler #(refresh! context)}
                       :error {:signal "error(QString)" :handler #(report! context % true)}}}]]
     [:label {:id "status" :label status :color (if error? "#ffb4a8" "#ffffff")}]
     [:file-chooser {:id "open-file" :on-accepted #(accept-image! context %)}]
     [:shortcut {:id "open-key" :sequences ["Ctrl+O"]
                 :on-activate #(open-dialog! context)}]
     [:shortcut {:id "rect-key" :sequences ["R"] :enabled has-image?
                 :on-activate #(choose-tool! context "rect")}]
     [:shortcut {:id "undo-key" :sequences ["Ctrl+Z"] :enabled can-undo?
                 :on-activate #(native/undo! (canvas context))}]
     [:shortcut {:id "redo-key" :sequences ["Ctrl+Shift+Z"] :enabled can-redo?
                 :on-activate #(native/redo! (canvas context))}]
     [:shortcut {:id "copy-key" :sequences ["Ctrl+C"] :enabled has-image?
                 :on-activate #(command! context native/copy! "Copied to clipboard"
                                         "Could not copy to the clipboard. Try again.")}]
     [:shortcut {:id "save-key" :sequences ["Ctrl+S"] :enabled has-image?
                 :on-activate #(command! context native/save! "Saved"
                                         "Could not save the image. Check the Pictures folder and try again.")}]]))

(defn- review! [manager id path]
  (when-let [{:keys [services]} (sessions/session manager id)]
    (let [trim (:trim services)
          window (qt/open-window!
                   (fn [] [:vbox {:padding 14 :spacing 10}
                           [:label {:label (str "Recording saved: " path)}]
                           [:button {:label "Keep original"
                                     :on-click #(object/invoke! trim "keepOriginal()" [])}]])
                   :title "Snitt — Review recording" :width 600 :height 130
                   :on-close (fn [_] (object/invoke! trim "keepOriginal()" []) false))]
      (sessions/set-review-window! manager id window))))

(defn- recording-controls! [manager id]
  (when-let [{:keys [services controls-window]} (sessions/session manager id)]
    (let [backend (:backend services)
          recording? (q/read-property backend "recording")]
      (cond
        (and recording? (not (and controls-window (qt/window-alive? controls-window))))
        (sessions/set-controls-window!
         manager id
         (qt/open-window!
          (fn [] [:hbox {:padding 10 :spacing 8}
                  [:label {:label "Recording"}]
                  [:button {:label "Stop" :on-click #(object/invoke! backend "finishRecording()" [])}]
                  [:button {:label "Cancel" :on-click #(object/invoke! backend "cancelRecording()" [])}]])
          :title "Snitt — Recording" :width 320 :height 70
          :on-close (fn [_] (object/invoke! backend "cancelRecording()" []) false)))
        (and (not recording?) controls-window (qt/window-alive? controls-window))
        (qt/close-window! controls-window)))))

(defn- create-editor! [id label on-close]
  (let [context {:window (atom nil)
                 :state (if (= id 1) state (r/atom (initial-state)))}
        services (native/create-services!)]
    (try
      (let [window (qt/open-window! #(editor context)
                                    :title (if label (str "Snitt — " label) "Snitt · Jolt development")
                                    :width 900 :height 640 :visible false
                                    :on-close (fn [_] (on-close) false))]
        (reset! (:window context) window)
        (let [canvas (canvas context)]
          (when-not (object/invoke! (:root services) "bindCanvas(QObject*)"
                                    [(value/object canvas)])
            (throw (ex-info "Could not bind editor document to session" {:id id})))
          (sync-canvas-size! context)
          (q/set-property! canvas "saveRoot"
                           (q/read-property (:settings services) "picturesRoot"))
          (refresh! context))
        {:window window :services services :context context
         :load-url #(open-url! context %)
         :on-error (fn [_ message] (report! context message true))
         :on-recording-saved review!
         :on-recording-changed recording-controls!})
      (catch Throwable e
        (when-let [window @(:window context)] (qt/close-window! window))
        (widget/dispose! (:root services))
        (throw e)))))

(defn start! [& {:keys [image capture-start]}]
  (native/install!)
  (reset! state (initial-state))
  (let [manager (sessions/manager create-editor! :start-capture capture-start)]
    (reset! runtime {:manager manager})
    (let [id (sessions/create! manager nil)
          {:keys [window services context]} (sessions/session manager id)]
      (swap! runtime assoc :window window :services services :context context)
      (qt/activate! window)
      (when image
        (when-not (native/load-image! (canvas context) image)
          (report! context (str "Could not open " image) true)))
      @runtime)))

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
