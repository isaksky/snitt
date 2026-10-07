(ns snitt.app
  "Jolt-owned editor sessions; each EditorCanvas remains its document owner."
  (:require [glimmer.ratom :as r]
            [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.tray :as tray]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [snitt.commands :as commands]
            [snitt.native :as native]
            [snitt.sessions :as sessions]
            [snitt.settings :as settings]))

(defn- initial-state []
  {:tool "rect" :status "Open or paste an image" :error? false
   :document-revision 0 :modal? false :text-draft? false
   :canvas-width 800 :canvas-height 500})

;; The first session remains addressable for the development smoke and parity
;; tests; every additional editor gets its own atom and independent Glimmer root.
(defonce state (r/atom (initial-state)))
(defonce runtime (atom nil))

(defn canvas
  ([] (when-let [window (:window @runtime)]
        (when (qt/window-alive? window)
          (let [handle (qt/find-by-id window "snitt-canvas")]
            (when (and handle (q/alive? handle)) handle)))))
  ([context] (when-let [window @(:window context)]
               (when (qt/window-alive? window)
                 (let [handle (qt/find-by-id window "snitt-canvas")]
                   (when (and handle (q/alive? handle)) handle))))))

(defn- item [context id]
  (when-let [window @(:window context)]
    (when (qt/window-alive? window)
      (let [handle (qt/find-by-id window id)]
        (when (and handle (q/alive? handle)) handle)))))

(defn- report! [context message error?]
  (swap! (:state context) assoc :status message :error? error?))

(defn- refresh! [context]
  (when-let [handle (canvas context)]
    (let [has-image? (q/read-property handle "hasImage")]
      (swap! (:state context)
             #(-> % (update :document-revision inc)
                  (assoc :status (if has-image?
                                   (str (q/read-property handle "imageWidth") " × "
                                        (q/read-property handle "imageHeight"))
                                   "Open or paste an image")
                         :error? false))))))

(defn- refresh-context! [context]
  (swap! (:state context) update :context-revision (fnil inc 0)))

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

(defn- result! [context action success-message failure-message]
  (let [result (action (canvas context))]
    (if (and result (not (and (string? result) (empty? result))))
      (do (report! context (if (string? result) result success-message) false)
          true)
      (do (report! context failure-message true)
          false))))

(defn command-state [context]
  (let [handle (canvas context)
        services @(:services context)
        backend (when (and (:backend services) (q/alive? (:backend services))) (:backend services))
        trim (when (and (:trim services) (q/alive? (:trim services))) (:trim services))
        manager (:manager @runtime)
        session (when manager (sessions/session manager (:id context)))
        document (fn [property fallback]
                   (if handle (q/read-property handle property) fallback))]
    {:text-draft? (:text-draft? @(:state context))
     :modal? (or (:modal? @(:state context))
                 (when-let [chooser (item context "open-file")]
                   (widget/dialog-visible? chooser)))
     :capturing? (or (and backend (q/read-property backend "capturing"))
                     (and manager (:capture (sessions/snapshot manager))))
     :recording? (and backend (q/read-property backend "recording"))
     :review? (and session (:review-window session)
                   (qt/window-alive? (:review-window session))
                   (q/read-property (:review-window session) "visible"))
     :review-can-edit? (and trim (:review-command context)
                            (not (q/read-property trim "busy")))
     :session-manager? (boolean manager)
     :has-image? (document "hasImage" false)
     :can-undo? (document "canUndo" false)
     :can-redo? (document "canRedo" false)
     :arranging? (document "arranging" false)
     :columns (document "columns" 1)
     :region-count (document "regionCount" 0)
     :selected-region (document "selectedRegion" -1)}))

(defn- run-effect! [context [effect & args]]
  (let [handle (canvas context)
        manager (:manager @runtime)]
    (case effect
      :tool (swap! (:state context) assoc :tool (first args))
      :ink (object/invoke! handle "setInkMode(QString)" args)
      :open-dialog (do (swap! (:state context) assoc :modal? true)
                       (widget/open-dialog! (item context "open-file")))
      :paste (result! context #(object/invoke! % "paste()" []) "Pasted"
                      "The clipboard has no image to paste.")
      :capture (apply sessions/capture! manager args)
      :annotate (object/invoke! handle "annotate()" [])
      :columns (q/set-property! handle "columns" (first args))
      :remove-region (object/invoke! handle "removeRegion(int)" args)
      :move-region (object/invoke! handle "moveRegion(int,int)" args)
      :undo (native/undo! handle)
      :redo (native/redo! handle)
      :finish (do
                (native/cancel! handle)
                (when (case (first args)
                        :copy (result! context native/copy! "Copied to clipboard"
                                       "Could not copy to the clipboard. Try again.")
                        :save (result! context native/save! "Saved"
                                       "Could not save the image. Check the Pictures folder and try again.")
                        false)
                  (sessions/close! manager (:id context))))
      :dismiss (when-not (native/cancel! handle)
                 (sessions/close! manager (:id context)))
      :review (if (= :cancel (first args))
                (object/invoke! (:trim @(:services context)) "keepOriginal()" [])
                (when-let [handler (:review-command context)]
                  (apply handler args))))))

(defn dispatch!
  "Invoke a command from any UI or external source. A fresh native/session
  snapshot is checked here, so calling a handler directly cannot bypass policy."
  [context action]
  (when-let [effects (commands/decide (command-state context) action)]
    (doseq [effect effects] (run-effect! context effect))
    true))

(defn dispatch-global!
  "Dispatch tray, native hotkey, or external application commands."
  [action]
  (when-let [manager (:manager @runtime)]
    (when-let [effects (commands/global-decide
                       {:session-manager? true
                        :capture? (boolean (:capture (sessions/snapshot manager)))} action)]
      (doseq [[effect] effects]
        (case effect
          :hotkey-capture (sessions/hotkey-capture! manager)
          :show-editor (sessions/show-editor! manager)))
      true)))

(defn- accept-image! [context url]
  (swap! (:state context) assoc :modal? false)
  (if (:has-image? (command-state context))
    (let [path (q/local-file-path (value/url url))
          label (last (str/split path #"[/\\]"))]
      (try (sessions/open-image! (:manager @runtime) url label)
           (catch Throwable _
             (report! context "Could not open this image. Choose a supported image file and try again." true))))
    (open-url! context url)))

(defn editor [context]
  (let [{:keys [tool status error?
                canvas-width canvas-height]} @(:state context)
        {:keys [has-image?] :as command-state} (command-state context)
        {:keys [shortcuts hints]} @settings/published
        binding (fn [action] (get shortcuts action []))
        hint (fn [action] (get hints action ""))
        enabled? (fn [action] (commands/enabled? command-state action))]
    [:vbox {:hexpand true :vexpand true :padding 10 :spacing 8}
     [:hbox {:spacing 6}
      [:button {:id "open" :label (str "Open" (when (seq (hint "openImage")) (str " (" (hint "openImage") ")")))
                :sensitive (enabled? "openImage") :on-click #(dispatch! context "openImage")}]
      [:button {:id "paste" :label "Paste" :sensitive (enabled? "pasteImage")
                :on-click #(dispatch! context "pasteImage")}]
      [:button {:id "capture" :label "Capture" :sensitive (enabled? "capture")
                :on-click #(dispatch! context "capture")}]
      [:button {:id "record" :label "Record" :sensitive (enabled? "captureVideo")
                :on-click #(dispatch! context "captureVideo")}]
      [:button {:id "rect" :label (str "Rectangle" (when (seq (hint "toolRectangle")) (str " (" (hint "toolRectangle") ")")))
                :checkable true :checked (= tool "rect")
                :sensitive (enabled? "toolRectangle") :on-click #(dispatch! context "toolRectangle")}]
      [:button {:id "arrow" :label "Arrow" :checkable true :checked (= tool "arrow")
                :sensitive (enabled? "toolArrow") :on-click #(dispatch! context "toolArrow")}]
      [:button {:id "undo" :label "Undo" :sensitive (enabled? "undo")
                :on-click #(dispatch! context "undo")}]
      [:button {:id "redo" :label "Redo" :sensitive (enabled? "redo")
                :on-click #(dispatch! context "redo")}]
      [:button {:id "copy" :label "Copy" :sensitive (enabled? "copyClose")
                :on-click #(dispatch! context "copyClose")}]
      [:button {:id "save" :label "Save" :sensitive (enabled? "saveClose")
                :on-click #(dispatch! context "saveClose")}]]
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
                       :arrangement {:signal "arrangementChanged()" :handler #(refresh-context! context)}
                       :error {:signal "error(QString)" :handler #(report! context % true)}}}]]
     [:label {:id "status" :label status :color (if error? "#ffb4a8" "#ffffff")}]
     [:file-chooser {:id "open-file" :on-accepted #(accept-image! context %)
                     :on-rejected #(swap! (:state context) assoc :modal? false)
                     :on-visible-change #(when-not (widget/dialog-visible? (item context "open-file"))
                                           (swap! (:state context) assoc :modal? false))}]
     (for [action commands/editor-actions]
       [:shortcut {:id (str "command-" action) :sequences (binding action)
                   :enabled (enabled? action)
                   :on-activate #(dispatch! context action)}])]))

(defn- review! [manager id path]
  (when-let [{:keys [context]} (sessions/session manager id)]
    (let [window (qt/open-window!
                   (fn [] [:vbox {:padding 14 :spacing 10}
                           [:label {:label (str "Recording saved: " path)}]
                           [:button {:label "Keep original"
                                     :on-click #(dispatch! context "reviewCancel")}]])
                   :title "Snitt — Review recording" :width 600 :height 130
                   :on-close (fn [_] (dispatch! context "reviewCancel") false))]
      (sessions/set-review-window! manager id window)
      (refresh-context! context))))

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
  (let [services (native/create-services!)
        context {:id id :window (atom nil) :services (atom services)
                 :state (if (= id 1) state (r/atom (initial-state)))}]
    (try
      (settings/register! services)
      (let [window (qt/open-window! #(editor context)
                                    :title (if label (str "Snitt — " label) "Snitt · Jolt development")
                                    :width 900 :height 640 :visible false
                                    :on-close (fn [_] (on-close) false))]
        (reset! (:window context) window)
        (let [canvas (canvas context)]
          (when-not (object/invoke! (:root services) "bindCanvas(QObject*)"
                                    [(value/object canvas)])
            (throw (ex-info "Could not bind editor document to session" {:id id})))
          (doseq [[target signal] [[(:backend services) "capturingChanged()"]
                                   [(:backend services) "recordingChanged()"]
                                   [(:trim services) "changed()"]]]
            (object/subscribe! (:root services) target signal
                               #(refresh-context! context)))
          (sync-canvas-size! context)
          (q/set-property! canvas "saveRoot"
                           (q/read-property (:settings services) "picturesRoot"))
          (when-let [{:keys [good-color bad-color]} @settings/published]
            (q/set-property! canvas "goodColor" (value/color good-color))
            (q/set-property! canvas "badColor" (value/color bad-color)))
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

(defn- settings-published! [{:keys [good-color bad-color pictures-root hints]}]
  (when-let [manager (:manager @runtime)]
    (doseq [{:keys [context]} (sessions/sessions manager)]
      (when-let [handle (canvas context)]
        (q/set-property! handle "goodColor" (value/color good-color))
        (q/set-property! handle "badColor" (value/color bad-color))
        (q/set-property! handle "saveRoot" pictures-root))))
  (when-let [{:keys [tray capture-action]} @runtime]
    (when (q/alive? tray)
      (tray/set-tooltip! tray (str "Snitt — Capture " (get hints "globalCapture")))
      (tray/update-action! capture-action :shortcut-hint (get hints "globalCapture")))))

(defn- settings-error! [message]
  (when-let [manager (:manager @runtime)]
    (doseq [{:keys [context]} (sessions/sessions manager)]
      (report! context message true)))
  (when-let [icon (:tray @runtime)]
    (when (q/alive? icon)
      (tray/notify! icon "Snitt settings not applied" message :kind :warning))))

(defn start! [& {:keys [image capture-start]}]
  (native/install!)
  (reset! state (initial-state))
  (settings/configure! :on-publish settings-published! :on-error settings-error!
                       :on-capture #(dispatch-global! "globalCapture"))
  (let [manager (sessions/manager create-editor! :start-capture capture-start)]
    (reset! runtime {:manager manager})
    (let [id (sessions/create! manager nil)
          {:keys [window services context]} (sessions/session manager id)]
      (swap! runtime assoc :window window :services services :context context)
      (when (tray/available?)
        (let [icon (tray/create! ":/icons/square.svg"
                                 :tooltip (str "Snitt — Capture "
                                               (get-in @settings/published [:hints "globalCapture"])))
              action (tray/add-action! icon :label "Capture"
                                        :shortcut-hint (get-in @settings/published [:hints "globalCapture"])
                                        :on-trigger #(dispatch-global! "globalCapture"))]
          (tray/add-action! icon :label "Show Snitt"
                            :on-trigger #(dispatch-global! "showEditor"))
          (tray/show! icon)
          (swap! runtime assoc :tray icon :capture-action action)))
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
      (finally (settings/clear!) (reset! runtime nil) (shutdown-agents)))))
