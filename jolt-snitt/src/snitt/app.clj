(ns snitt.app
  "Jolt-owned editor sessions and documents; native canvases render projections."
  (:require [snitt.trim :as trim]
            [glimmer.ratom :as r]
            [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.tray :as tray]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [snitt.arrangement :as arrangement]
            [snitt.commands :as commands]
            [snitt.document :as document]
            [snitt.geometry :as geometry]
            [snitt.lifecycle :as lifecycle]
            [snitt.native :as native]
            [snitt.review :as review]
            [snitt.sessions :as sessions]
            [snitt.settings :as settings]))

(defn- initial-state []
  {:status "Open or paste an image" :error? false
   :document-revision 0 :modal? false :draft nil :draft-content-height 0
   :canvas-width 800 :canvas-height 500 :arrangement nil})

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

(declare command-state refresh-context! start-text! change-arrangement!)

(defn- current-geometry [context]
  (let [handle (canvas context)
        area (item context "snitt-pointer")]
    (when (and handle area)
      (geometry/image-geometry
       {:width (q/read-property area "width")
        :height (q/read-property area "height")
        :image-width (q/read-property handle "imageWidth")
        :image-height (q/read-property handle "imageHeight")}))))

(defn- sync-geometry! [context]
  (when-let [image (current-geometry context)]
    (native/set-geometry! (canvas context) image)))

(defn- sync-tool-sizes! [context]
  (when-let [handle (canvas context)]
    (native/set-tool-sizes! handle @(:interaction context))))

(defn- transition! [context step]
  (let [[next effects] (step @(:interaction context))
        handle (canvas context)]
    (reset! (:interaction context) next)
    (doseq [{:keys [op point tool index from to drop] :as effect} effects]
      (case op
        :preview (native/set-preview! handle effect)
        :clear-preview (native/clear-preview! handle)
        :draw (native/draw! handle effect)
        :text-request (start-text! context (first point) (second point))
        :select-region (change-arrangement! context arrangement/select index)
        :move-region (change-arrangement! context arrangement/move from to)
        :arrangement-preview (native/set-drop-region! handle drop)
        :tool (do (q/set-property! handle "tool" tool)
                  (refresh-context! context))
        :sizes (do (sync-tool-sizes! context)
                   (refresh-context! context))))
    effects))

(defn- cancel-gesture! [context]
  (boolean (seq (transition! context geometry/cancel))))

(defn- region-at [context image x y]
  (if image
    (native/region-at (canvas context) (geometry/image-point image x y))
    -1))

(defn- refresh! [context]
  (when-let [handle (canvas context)]
    (let [has-image? (q/read-property handle "hasImage")]
      (reset! (:interaction context)
              (geometry/image-changed @(:interaction context)
                                      (q/read-property handle "imageWidth")
                                      (q/read-property handle "imageHeight")))
      (sync-geometry! context)
      (sync-tool-sizes! context)
      (swap! (:state context)
             #(-> % (update :document-revision inc)
                  (assoc :status (if has-image?
                                   (str (q/read-property handle "imageWidth") " × "
                                        (q/read-property handle "imageHeight"))
                                   "Open or paste an image")
                         :error? false)
                  (cond-> (zero? (q/read-property handle "regionCount"))
                    (assoc :arrangement nil)))))))

(defn- refresh-context! [context]
  (swap! (:state context) update :context-revision (fnil inc 0)))

(defn- sync-canvas-size! [context]
  (when-let [area (item context "snitt-pointer")]
    (let [width (q/read-property area "width")
          height (q/read-property area "height")]
      (when (and (pos? width) (pos? height)
                 (not= [width height]
                       ((juxt :canvas-width :canvas-height) @(:state context))))
        (swap! (:state context) assoc :canvas-width width :canvas-height height))
      (sync-geometry! context))))

(defn- focus-canvas! [context]
  (when-let [handle (canvas context)] (widget/focus! handle)))

(defn- cancel-text! [context]
  (when (:draft @(:state context))
    (swap! (:state context) assoc :draft nil :draft-content-height 0)
    (qt/later! 0 #(focus-canvas! context))
    true))

(defn- commit-text! [context]
  (when-let [{:keys [x y width text]} (:draft @(:state context))]
    (let [handle (canvas context)]
      (document/edit! handle {:kind :text :start [x y]
                              :end [(+ x width) (q/read-property handle "imageHeight")]
                              :text text :font-size (q/read-property handle "textSize")}))
    (cancel-text! context)
    true))

(defn- start-text! [context x y]
  (let [handle (canvas context)
        image-width (q/read-property handle "imageWidth")
        image-height (q/read-property handle "imageHeight")
        size (q/read-property handle "textSize")
        width (min 360 image-width)]
    (swap! (:state context) assoc
           :draft {:x (min x (- image-width width))
                   :y (max 0 (min y (- image-height (max 32 (* size 1.5)))))
                   :width width :text ""}
           :draft-content-height 0)
    (qt/later! 0 #(when-let [editor (item context "annotation-text")]
                    (widget/focus! editor)))))

(defn- adjust-size! [context delta]
  (when (and delta (not (zero? delta)))
    (let [handle (canvas context)]
      (transition! context #(geometry/wheel % delta
                                            (q/read-property handle "imageWidth")
                                            (q/read-property handle "imageHeight")
                                            (:arranging? (command-state context)))))))

(defn- wheel-size! [context event]
  (let [tool (:tool @(:interaction context))]
    (when (and (not (:arranging? (command-state context)))
               (#{"rect" "arrow" "text" "blur"} tool))
      (adjust-size! context (let [angle (:angle-y event)]
                              (if (zero? angle) (* 8 (:pixel-y event)) angle)))
      true)))

(defn- draft-geometry [context]
  (when-let [{:keys [x y width]} (:draft @(:state context))]
    (let [handle (canvas context)
          image-width (q/read-property handle "imageWidth")
          image-height (q/read-property handle "imageHeight")
          scale (q/read-property handle "imageScale")
          canvas-width (:canvas-width @(:state context))
          canvas-height (:canvas-height @(:state context))
          size (q/read-property handle "textSize")]
      {:x (+ (/ (- canvas-width (* image-width scale)) 2) (* x scale))
       :y (+ (/ (- canvas-height (* image-height scale)) 2) (* y scale))
       :width (* width scale)
       :height (min (max (* size 1.5 scale) (:draft-content-height @(:state context)))
                    (* (- image-height y) scale))
       :font-size (max 1 (Math/round (* size scale)))})))

(defn- resize-text! [context event phase]
  (let [draft (:draft @(:state context))
        scale (q/read-property (canvas context) "imageScale")]
    (case phase
      :press (do (reset! (:resize context) {:global-x (:global-x event)
                                           :width (:width draft)}) true)
      :move (when-let [{:keys [global-x width]} @(:resize context)]
              (let [handle (canvas context)
                    maximum (- (q/read-property handle "imageWidth") (:x draft))
                    minimum (min maximum (max 24 (* 2 (q/read-property handle "textSize"))))]
                (swap! (:state context) assoc-in [:draft :width]
                       (max minimum (min maximum (+ width (/ (- (:global-x event) global-x) scale))))))
              true)
      :release (do (reset! (:resize context) nil)
                   (qt/later! 0 #(when-let [editor (item context "annotation-text")]
                                   (widget/focus! editor)))
                   true))))

(defn- open-url! [context url]
  (if (native/load-url! (canvas context) (value/url url))
    (do (swap! (:state context) assoc :arrangement nil) true)
    (do (report! context "Could not open this image. Choose a supported image file and try again." true)
        false)))

(defn- result! [context action success-message failure-message]
  (let [result (action (canvas context))]
    (if (and result (not (and (string? result) (empty? result))))
      (do (report! context (if (string? result) result success-message) false)
          true)
      (do (report! context failure-message true)
          false))))

(defn reveal-saved! [backend path]
  (object/invoke! backend "revealFile(QString)" [path]))

(defn- finish-save! [context manager handle]
  (let [{:keys [ok? path error]} (native/save-result! handle)]
    (if-not ok?
      (report! context (or error "Could not save the image. Check the Pictures folder and try again.") true)
      (if (reveal-saved! (:backend @(:services context)) path)
        (do (report! context path false)
            (sessions/close! manager (:id context)))
        (do (report! context (str "Screenshot saved at " path
                                  ", but could not reveal it in the file manager.") true)
            (swap! (:state context) assoc :saved-path path :modal? true)
            (widget/open-dialog! (item context "reveal-dialog")))))))

(defn command-state [context]
  (let [handle (canvas context)
        services @(:services context)
        backend (when (and (:backend services) (q/alive? (:backend services))) (:backend services))
        trim (when (and (:trim services) (q/alive? (:handle (:trim services)))) (:trim services))
        manager (:manager @runtime)
        session (when manager (sessions/session manager (:id context)))
        document (fn [property fallback]
                   (if handle (q/read-property handle property) fallback))
        history (or (document/snapshot handle) (document/empty-state))
        layout (:arrangement @(:state context))]
    {:text-draft? (boolean (:draft @(:state context)))
     :modal? (or (:modal? @(:state context))
                 (when-let [chooser (item context "open-file")]
                   (widget/dialog-visible? chooser)))
     :capturing? (or (and backend (q/read-property backend "capturing"))
                     (and manager (:capture (sessions/snapshot manager))))
     :recording? (and manager (sessions/recording? manager (:id context)))
     :review? (and session (:review-window session)
                   (qt/window-alive? (:review-window session))
                   (q/read-property (:review-window session) "visible"))
     :review-can-edit? (and trim @(:review context)
                            (review/editable? @(:state @(:review context))
                                              (trim/property trim "busy")))
     :session-manager? (boolean manager)
     :has-image? (document "hasImage" false)
     :can-undo? (document/can-undo? history)
     :can-redo? (document/can-redo? history)
     :arranging? (= :arrange (:phase layout))
     :columns (:columns layout 1)
     :region-count (count (:order layout))
     :selected-region (:selected layout -1)}))

(defn- apply-arrangement! [context candidate]
  (when candidate
    (let [handle (canvas context)]
      (when (object/invoke! handle "applyExternalArrangement(QString,int,int,bool)"
                            [(str/join "," (:order candidate)) (:columns candidate)
                             (:selected candidate) (= :arrange (:phase candidate))])
        (swap! (:state context) assoc :arrangement candidate)
        true))))

(defn- change-arrangement! [context f & args]
  (apply-arrangement! context (apply f (:arrangement @(:state context)) args)))

(defn- remove-region! [context index]
  (let [layout (:arrangement @(:state context))]
    (if (= 1 (count (:order layout)))
      (when (and (= :arrange (:phase layout)) (= index (:selected layout)))
        (object/invoke! (canvas context) "clear()" [])
        (swap! (:state context) assoc :arrangement nil)
        true)
      (change-arrangement! context arrangement/remove-region index))))

(defn- return-to-arrangement! [context]
  (change-arrangement! context arrangement/arrange)
  (focus-canvas! context))

(defn- run-effect! [context [effect & args]]
  (let [handle (canvas context)
        manager (:manager @runtime)]
    (case effect
      :tool (transition! context #(geometry/select-tool % (first args)))
      :ink (object/invoke! handle "setInkMode(QString)" (vec args))
      :open-dialog (do (swap! (:state context) assoc :modal? true)
                       (widget/open-dialog! (item context "open-file")))
      :paste (when (result! context #(object/invoke! % "paste()" []) "Pasted"
                            "The clipboard has no image to paste.")
               (cancel-text! context)
               (swap! (:state context) assoc :arrangement nil))
      :capture (apply sessions/capture! manager args)
      :annotate (change-arrangement! context arrangement/annotate)
      :return-arrange (if (or (q/read-property handle "canUndo")
                              (q/read-property handle "canRedo"))
                        (do (swap! (:state context) assoc :modal? true)
                            (widget/open-dialog! (item context "rearrange-dialog")))
                        (return-to-arrangement! context))
      :columns (change-arrangement! context arrangement/columns (first args))
      :remove-region (remove-region! context (first args))
      :move-region (apply change-arrangement! context arrangement/move args)
      :undo (native/undo! handle)
      :redo (native/redo! handle)
      :finish (do
                (cancel-gesture! context)
                (native/cancel! handle)
                (case (first args)
                  :copy (when (result! context native/copy! "Copied to clipboard"
                                       "Could not copy to the clipboard. Try again.")
                          (sessions/close! manager (:id context)))
                  :save (finish-save! context manager handle)
                  nil))
      :dismiss (when-not (or (cancel-gesture! context) (native/cancel! handle))
                 (sessions/close! manager (:id context)))
      :review (when-let [owner @(:review context)]
                (apply review/command! owner args)))))

(defn dispatch!
  "Invoke a command from any UI or external source. A fresh native/session
  snapshot is checked here, so calling a handler directly cannot bypass policy."
  [context action]
  (when-let [effects (commands/decide (command-state context) action)]
    (doseq [effect effects] (run-effect! context effect))
    true))

(defn- dispatch-toolbar! [context action]
  (let [state (command-state context)
        committable? (contains? #{"toolCut" "toolRectangle" "toolHighlight" "toolText"
                                 "toolArrow" "toolPixelate" "toolErase" "goodMode" "badMode"
                                 "copyClose" "saveClose" "dismiss"} action)
        effective (if (and (:text-draft? state) committable?)
                    (assoc state :text-draft? false) state)]
    (when-let [effects (commands/decide effective action)]
      (when (:text-draft? state) (commit-text! context))
      (doseq [effect effects] (run-effect! context effect))
      true)))

(defn paste-image! [context]
  "Paste can replace an unfinished draft; only a successful native paste clears it."
  (let [success? (result! context #(object/invoke! % "paste()" []) "Pasted"
                          "The clipboard has no image to paste.")]
    (when success?
      (cancel-text! context)
      (swap! (:state context) assoc :arrangement nil))
    success?))

(defn dispatch-global!
  "Dispatch tray, native hotkey, or external application commands."
  [action]
  (if (= action "quit") (do (qt/quit!) true)
    (when-let [manager (:manager @runtime)]
      (if (= action "capture")
        (sessions/capture! manager false false)
        (when-let [effects (commands/global-decide
                           {:session-manager? true
                            :capture? (boolean (:capture (sessions/snapshot manager)))} action)]
          (doseq [[effect] effects]
            (case effect
              :hotkey-capture (sessions/hotkey-capture! manager)
              :show-editor (sessions/show-editor! manager)))
          true)))))

(defn dispatch-arguments!
  ([arguments] (dispatch-arguments! arguments false))
  ([arguments startup?]
   (when-let [manager (:manager @runtime)]
     (let [[action path] ((if startup? lifecycle/startup-command lifecycle/command) arguments)]
       (case action
         :quit (qt/quit!)
         :background nil
         :show (dispatch-global! "showEditor")
         :image (try (sessions/open-image! manager
                                          (object/invoke! (:endpoint @runtime) "fileUrl(QString)" [path])
                                          (last (str/split path #"[/\\]")))
                     (catch Throwable e
                       (when-let [icon (:tray @runtime)]
                         (tray/notify! icon "Could not open editor" (ex-message e) :kind :warning))))
         :capture (sessions/capture! manager false false))))))

(defn- accept-image! [context url]
  (swap! (:state context) assoc :modal? false)
  (if (:has-image? (command-state context))
    (let [path (q/local-file-path (value/url url))
          label (last (str/split path #"[/\\]"))]
      (try (sessions/open-image! (:manager @runtime) url label)
           (catch Throwable _
             (report! context "Could not open this image. Choose a supported image file and try again." true))))
    (open-url! context url)))

(def annotation-tools
  [["toolCut" "cut" "Cut" "cut"]
   ["toolRectangle" "rect" "Rectangle" "rectangle"]
   ["toolHighlight" "highlight" "Highlight" "highlight"]
   ["toolText" "text" "Text" "text"]
   ["toolArrow" "arrow" "Arrow" "arrow"]
   ["toolPixelate" "blur" "Pixelate" "pixelate"]
   ["toolErase" "erase" "Smart erase" "smart-erase"]])

(defn- size-hint [handle tool draft?]
  (cond
    draft? (str (q/read-property handle "textSize") " px · "
                (if (str/includes? (System/getProperty "os.name") "Mac") "⌘" "Ctrl+")
                "Enter to place")
    (= tool "cut") "↔ removes columns · ↕ removes rows"
    (#{"rect" "arrow"} tool) (str (q/read-property handle "strokeWidth") " px · Mouse wheel to adjust")
    (= tool "blur") (str (q/read-property handle "pixelBlockSize") " px · Mouse wheel to adjust")
    (= tool "erase") "Fills with the color where you start"
    (= tool "highlight") ""
    :else (str (q/read-property handle "textSize") " px · Mouse wheel to adjust")))

(defn editor [context]
  (let [{:keys [status error? draft canvas-width canvas-height]} @(:state context)
        tool (:tool @(:interaction context))
        {:keys [has-image? arranging?] :as command-state} (command-state context)
        {:keys [shortcuts hints]} @settings/published
        handle (canvas context)
        binding (fn [action] (get shortcuts action []))
        hint (fn [action] (get hints action ""))
        enabled? (fn [action] (boolean (commands/enabled? command-state action)))
        toolbar-enabled? (fn [action] (boolean (commands/enabled? (assoc command-state :text-draft? false) action)))
        action! (fn [action] #(dispatch-toolbar! context action))
        geometry (when (and draft handle) (draft-geometry context))
        color-mode (when handle (q/read-property handle "colorMode"))
        ink-color (when handle (:value (q/read-property handle "ink")))
        mode-color (fn [mode] (when handle (:value (q/read-property handle (str mode "Color")))))]
    [:vbox {:hexpand true :vexpand true :padding 10 :spacing 8}
     [:flow {:id "capture-toolbar" :hexpand true :spacing 6}
      (for [[id label action] [["open" "Open" "openImage"] ["paste" "Paste" "pasteImage"]
                               ["capture" "Capture" "capture"] ["capture-multiple" "Multiple regions" "captureMultiple"]
                               ["record" "Record" "captureVideo"]]]
        [:button {:key id :id id :label label :accessible-name label
                  :tooltip (str label " (" (hint action) ")")
                  :sensitive (enabled? action) :on-click #(dispatch! context action)}])]
     (if arranging?
       [:flow {:id "arrangement-toolbar" :hexpand true :spacing 4}
        (for [[id label action] [["arrange" "Annotate" "arrange"] ["columns-less" "−" "columnsLess"]
                                 ["columns-more" "+" "columnsMore"] ["region-left" "←" "moveRegionLeft"]
                                 ["region-right" "→" "moveRegionRight"] ["region-remove" "Remove" "removeRegion"]]]
          [:button {:key id :id id :label label :accessible-name (str label " region")
                    :tooltip (str label " (" (hint action) ")")
                    :sensitive (enabled? action) :on-click #(dispatch! context action)}])]
       [:flow {:id "annotation-toolbar" :hexpand true :spacing 8}
        (when (pos? (:region-count command-state))
          [:button {:id "return-arrange" :label "Back to Arrange"
                    :accessible-name "Back to Arrange"
                    :sensitive (enabled? "returnArrange")
                    :on-click #(dispatch! context "returnArrange")}])
        [:hbox {:id "annotation-tools" :spacing 2}
         (for [[action name label glyph] annotation-tools]
           [:vbox {:key name :spacing 2 :preferred-width 42}
            [:button {:id (if (#{"rect" "arrow"} name) name (str "tool-" name))
                      :label (str label " (" (hint action) ")")
                      :icon (str "qrc:/icons/annotation/" glyph ".svg")
                      :icon-only true :accessible-name label
                      :tooltip (str label " (" (hint action) ")")
                      :checkable true :checked (= tool name) :preferred-width 40
                      :sensitive (toolbar-enabled? action) :on-click (action! action)}]
            [:label {:id (str "shortcut-" name) :label (hint action) :font-size 11
                     :color "#9ba5b5"}]])]
        [:hbox {:id "history-buttons" :spacing 2}
         (for [[id label action icon] [["undo" "Undo" "undo" "undo-2"]
                                       ["redo" "Redo" "redo" "redo-2"]]]
           [:button {:key id :id id :label label :icon (str "qrc:/icons/" icon ".svg")
                     :icon-only true :accessible-name label
                     :tooltip (str label " (" (hint action) ")")
                     :sensitive (enabled? action) :on-click (action! action)}])]
        [:vbox {:id "mode-group" :spacing 2}
         [:hbox {:spacing 2}
          (for [[action mode label] [["goodMode" "good" "Good"] ["badMode" "bad" "Bad"]]]
            [:button {:key mode :id (str "ink-" mode) :label label
                      :accessible-name (str label " mode") :mode mode
                      :mode-color (or (mode-color mode) "transparent")
                      :tooltip (str label " mode (" (hint action) ")")
                      :checkable true :checked (= color-mode mode)
                      :sensitive (toolbar-enabled? action) :on-click (action! action)}])]
         [:label {:label "Color Mode" :font-size 11 :color "#9ba5b5"}]]
        [:vbox {:id "finish-group" :spacing 2}
         [:hbox {:spacing 4}
          [:button {:id "save" :label "Save" :accessible-name "Save image and close"
                    :tooltip (str "Save and close (" (hint "saveClose") ")")
                    :sensitive (toolbar-enabled? "saveClose") :on-click (action! "saveClose")}]
          [:button {:id "copy" :label "Copy" :accessible-name "Copy image and close"
                    :highlighted true :tooltip (str "Copy and close (" (hint "copyClose") ")")
                    :sensitive (toolbar-enabled? "copyClose") :on-click (action! "copyClose") }]]
         [:label {:label "Finish (will close)" :font-size 11 :color "#9ba5b5"}]]])
     [:label {:id "annotation-hint" :label (if has-image? (size-hint handle tool (boolean draft)) "Capture or open an image")
              :color "#9ba5b5" :font-size 12}]
     [:overlay {:id "editor-stage" :hexpand true :vexpand true :clip true
                :preferred-width 800 :preferred-height 500}
      [:pointer-area {:id "snitt-pointer" :anchor "fill"
                      :buttons (value/flags "LeftButton") :sensitive has-image?
                      :qt/subscriptions
                      {:width {:signal "widthChanged()" :handler #(sync-canvas-size! context)}
                       :height {:signal "heightChanged()" :handler #(sync-canvas-size! context)}}
                      :on-pointer-press (fn [{:keys [x y]}]
                                          (if (:draft @(:state context))
                                            (do (commit-text! context) true)
                                            (do (focus-canvas! context)
                                                (if arranging?
                                                  (let [image (current-geometry context)]
                                                    (transition! context
                                                                 #(geometry/arrangement-press % image x y
                                                                                              (region-at context image x y))))
                                                  (transition! context
                                                               #(geometry/press % (current-geometry context) x y)))
                                                true)))
                      :on-pointer-move (fn [{:keys [x y]}]
                                         (if arranging?
                                           (let [image (current-geometry context)]
                                             (transition! context
                                                          #(geometry/arrangement-drag % image x y
                                                                                      (region-at context image x y))))
                                           (transition! context
                                                        #(geometry/drag % (current-geometry context) x y))))
                      :on-pointer-release (fn [{:keys [x y]}]
                                            (if arranging?
                                              (let [image (current-geometry context)]
                                                (transition! context
                                                             #(geometry/arrangement-release % image x y
                                                                                            (region-at context image x y))))
                                              (transition! context
                                                           #(geometry/release % (current-geometry context) x y))))
                      :on-pointer-cancel (fn [_]
                                           (cancel-gesture! context))
                      :on-wheel #(wheel-size! context %)}
       [:snitt/canvas {:id "snitt-canvas" :width canvas-width :height canvas-height :tool tool
                       :qt/subscriptions
                       {:image {:signal "imageChanged()" :handler #(refresh! context)}
                        :arrangement {:signal "arrangementChanged()" :handler #(refresh-context! context)}
                        :size {:signal "sizeChanged()" :handler #(refresh-context! context)}
                        :ink {:signal "inkChanged()" :handler #(refresh-context! context)}
                        :regions-loaded {:signal "externalRegionsLoaded(int)"
                                         :handler #(swap! (:state context) assoc
                                                          :arrangement (arrangement/captured %))}
                        :region-selected {:signal "externalRegionSelected(int)"
                                          :handler #(change-arrangement! context arrangement/select %)}
                        :region-moved {:signal "externalRegionMoved(int,int)"
                                       :handler #(change-arrangement! context arrangement/move %1 %2)}
                        :text {:signal "textRequested(qreal,qreal)" :handler #(start-text! context %1 %2)}
                        :error {:signal "error(QString)" :handler #(report! context % true)}}}]]
      (when geometry
        [:rectangle {:id "annotation-text-frame" :x (:x geometry) :y (:y geometry)
                     :width (:width geometry) :height (:height geometry)
                     :z 2 :clip true :color "#dd181b21" :border-color "#9ba5b5"
                     :border-width 1 :radius 2}
         [:scroll {:id "annotation-text-scroll" :anchor "fill"
                   :horizontal-scrollbar "never" :vertical-scrollbar "as-needed"
                   :on-wheel #(wheel-size! context %)}
          [:text-area {:id "annotation-text" :text (:text draft) :padding 0
                       :font-family (q/read-property handle "annotationFontFamily")
                       :font-size (:font-size geometry) :font-weight 600
                       :color ink-color :selection-color "#32547b"
                       :accessible-name "Annotation text"
                       :on-change #(swap! (:state context) assoc-in [:draft :text] %)
                       :on-cancel #(cancel-text! context)
                       :on-commit #(commit-text! context)
                       :on-wheel #(wheel-size! context %)
                       :qt/subscriptions
                       {:content-height {:property "contentHeight"
                                         :handler #(swap! (:state context) assoc :draft-content-height %)}}}]]])
      (when geometry
        [:pointer-area {:id "annotation-text-resize-handle"
                        :x (- (+ (:x geometry) (:width geometry)) 4)
                        :y (:y geometry) :width 16
                        :height (max 24 (:height geometry)) :z 3
                        :hover true :prevent-stealing true
                        :cursor (value/enum "SizeHorCursor")
                        :on-pointer-press #(resize-text! context % :press)
                        :on-pointer-move #(resize-text! context % :move)
                        :on-pointer-release #(resize-text! context % :release)
                        :on-pointer-cancel #(resize-text! context % :release)}
         [:rectangle {:anchor "center" :width 6 :height 24 :radius 3 :color "#dfe5ed"}]])]
     [:label {:id "status" :label status :color (if error? "#ffb4a8" "#ffffff")}]
     [:file-chooser {:id "open-file" :on-accepted #(accept-image! context %)
                     :on-rejected #(swap! (:state context) assoc :modal? false)
                     :on-visible-change #(when-not (widget/dialog-visible? (item context "open-file"))
                                           (swap! (:state context) assoc :modal? false))}]
     [:dialog {:id "rearrange-dialog" :title "Return to Arrange?"
               :on-accepted #(do (swap! (:state context) assoc :modal? false)
                                 (return-to-arrangement! context))
               :on-rejected #(swap! (:state context) assoc :modal? false)}
      [:label {:label "Returning to Arrange will discard your annotations and cuts. Your captured regions will be kept."}]
      [:hbox {:spacing 8}
       [:button {:id "keep-editing" :label "Keep editing"
                 :on-click #(widget/reject-dialog! (item context "rearrange-dialog"))}]
       [:button {:id "discard-edits" :label "Discard edits"
                 :on-click #(widget/accept-dialog! (item context "rearrange-dialog"))}]]]
     [:dialog {:id "reveal-dialog" :title "Could not reveal screenshot"
               :on-accepted #(swap! (:state context) assoc :modal? false)
               :on-rejected #(swap! (:state context) assoc :modal? false)}
      [:label {:label (str "Screenshot saved at " (:saved-path @(:state context))
                           ", but could not reveal it in the file manager.")}]
      [:button {:id "reveal-ok" :label "OK"
                :on-click #(widget/accept-dialog! (item context "reveal-dialog"))}]]
     (for [action commands/editor-actions]
       [:shortcut {:id (str "command-" action) :sequences (binding action)
                   :enabled (enabled? action)
                   :on-activate #(dispatch! context action)}])]))

(defn- review! [manager id _path]
  (when-let [{:keys [context]} (sessions/session manager id)]
    (let [owner (review/open! (:root @(:services context))
                              (:trim @(:services context))
                              #(when-let [current @(:review context)]
                                 (review/dispose! current)
                                 (reset! (:review context) nil)
                                 (sessions/set-review-window! manager id nil)
                                 (refresh-context! context)))]
      (reset! (:review context) owner)
      (sessions/set-review-window! manager id @(:window owner))
      (refresh-context! context))))

(defn- create-editor! [id label on-close]
  (let [services (native/create-services!)
        context {:id id :window (atom nil) :services (atom services)
                 :review (atom nil)
                 :resize (atom nil)
                 :interaction (atom (geometry/initial-state))
                 :state (if (= id 1) state (r/atom (initial-state)))}]
    (try
      (settings/register! services)
      (let [window (qt/open-window! #(editor context)
                                    :title (if label (str "Snitt — " label) "Snitt · Jolt development")
                                    :width 900 :height 640 :visible false
                                    :on-close (fn [_] (on-close) false))]
        (reset! (:window context) window)
        (let [canvas (canvas context)]
          (document/create! canvas)
          (when-not (object/invoke! (:root services) "bindCanvas(QObject*)"
                                    [(value/object canvas)])
            (throw (ex-info "Could not bind editor document to session" {:id id})))
          (doseq [[target signal] [[(:backend services) "capturingChanged()"]
                                   [(:backend services) "recordingChanged()"]
                                   [(:handle (:trim services)) "changed()"]]]
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
         :on-close-document #(document/dispose! (canvas context))
         :on-error (fn [_ message] (report! context message true))
         :on-close-review #(when-let [owner @(:review context)]
                             (review/dispose! owner)
                             (reset! (:review context) nil))
         :on-recording-saved review!})
      (catch Throwable e
        (when-let [handle (canvas context)] (document/dispose! handle))
        (when-let [window @(:window context)] (qt/close-window! window))
        (widget/dispose! (:root services))
        (throw e)))))

(defn- settings-published! [{:keys [good-color bad-color pictures-root hints]}]
  (when-let [manager (:manager @runtime)]
    (when-let [picker (:picker (sessions/snapshot manager))]
      (swap! (:state picker) update :settings-revision (fnil inc 0)))
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

(defn start! [& {:keys [image capture-start background]}]
  (native/install!)
  (reset! state (initial-state))
  (settings/configure! :on-publish settings-published! :on-error settings-error!
                       :on-capture #(dispatch-global! "globalCapture"))
  (let [manager (sessions/manager create-editor! :start-capture capture-start)
        host (native/create-services!)]
    (reset! runtime {:manager manager :host host})
    (settings/register! host)
    (when-not background
      (let [id (sessions/create! manager nil)
            {:keys [window services context]} (sessions/session manager id)]
        (swap! runtime assoc :window window :services services :context context)
        (qt/activate! window)
        (when image
          (when-not (native/load-image! (canvas context) image)
            (report! context (str "Could not open " image) true)))))
    (when (tray/available?)
      (let [icon (tray/create! ":/icons/square.svg"
                               :tooltip (str "Snitt — Capture "
                                             (get-in @settings/published [:hints "globalCapture"]))
                               :on-activate #(when (= % :double-click)
                                               (dispatch-global! "capture")))
            action (tray/add-action! icon :label "Capture region"
                                      :shortcut-hint (get-in @settings/published [:hints "globalCapture"])
                                      :on-trigger #(dispatch-global! "capture"))
            show-action (tray/add-action! icon :label "Open editor"
                                          :on-trigger #(dispatch-global! "showEditor"))]
        (tray/add-separator! icon)
        (let [quit-action (tray/add-action! icon :label "Quit Snitt"
                                            :on-trigger #(dispatch-global! "quit"))]
          (tray/show! icon)
          (swap! runtime assoc :tray icon :capture-action action
                 :show-action show-action :quit-action quit-action))))
    @runtime))

(defn -main [& args]
  (let [smoke? (= args ["--smoke-test"])
        result (atom nil)]
    (try
      (qt/run-application!
       #(do
          (native/install!)
          (if smoke?
            (do (start!)
                (qt/later! 250
                  (fn []
                    (when-not (and (qt/window-alive? (:window @runtime))
                                   (q/alive? (canvas))
                                   (q/alive? (get-in @runtime [:services :backend])))
                      (throw (ex-info "Snitt Jolt smoke failed" {})))
                    (when-let [path (System/getenv "SNITT_JOLT_IMAGE")]
                      (let [handle (canvas)]
                        (when-not (and (native/load-image! handle path)
                                       (document/edit! handle {:kind :annotation :tool "rect"
                                                               :start [10 10] :end [40 40]
                                                               :stroke-width 4})
                                       (document/undo! handle) (document/redo! handle)
                                       (native/copy! handle) (seq (native/save! handle)))
                          (throw (ex-info "Snitt Jolt document smoke failed" {})))))
                    (qt/quit!))))
            (let [endpoint (widget/create! :snitt/lifecycle {})
                  arguments (lifecycle/absolute-arguments endpoint args)
                  status (object/invoke! endpoint "start(QStringList)" [arguments])]
              (reset! result status)
              (case status
                "owner" (do
                          (start! :background true)
                          (swap! runtime assoc :endpoint endpoint)
                          (object/subscribe! endpoint endpoint "commandReceived(QStringList)"
                                             dispatch-arguments!)
                          (qt/later! 200 #(dispatch-arguments! arguments true)))
                ("forwarded" "quit") (qt/later! 0 qt/quit!)
                "error" (qt/later! 0 qt/quit!)))))
       :app-id (if (System/getenv "SNITT_JOLT_PRODUCTION")
                 "snitt" "snitt-jolt-development")
       :keep-alive true
       :auto-quit-ms (when smoke? 5000))
      (when (= "error" @result)
        (throw (ex-info "Another Snitt Jolt instance is starting or unresponsive." {})))
      (finally (settings/clear!) (reset! runtime nil) (shutdown-agents)))))
