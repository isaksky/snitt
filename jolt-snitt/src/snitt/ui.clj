(ns snitt.ui
  "Editor presentation matching Main.qml; application commands remain in snitt.app."
  (:require [glimmer-qt.value :as value]))

(def center (value/flags "AlignHCenter"))
(def top (value/flags "AlignTop"))
(def tab-focus (value/enum "TabFocus"))

(def annotation-tools
  [["toolCut" "cut" "Cut" "cut"]
   ["toolRectangle" "rect" "Rectangle" "rectangle"]
   ["toolHighlight" "highlight" "Highlight" "highlight"]
   ["toolText" "text" "Text" "text"]
   ["toolArrow" "arrow" "Arrow" "arrow"]
   ["toolPixelate" "blur" "Pixelate" "pixelate"]
   ["toolErase" "erase" "Smart erase" "smart-erase"]])

(defn caption [id text]
  [:label {:id id :label text :font-size 12 :color "#9ba5b5"
           :text-alignment (value/enum "AlignHCenter") :hexpand true}])

(defn header [{:keys [arranging? region-count columns tool color-mode notice hint-text
                     hints shortcuts pictures-root enabled? toolbar-enabled? dispatch! action! mode-color]}]
  (let [hint #(get hints % "")
        underline? (fn [action key] (boolean (some #{key} (get shortcuts action))))
        finish (fn [id label action primary?]
                 [:action-button {:id id :label label :accessible-name (str label " image and close")
                                  :appearance (if primary? "primary" "action")
                                  :shortcut-label true
                                  :font-weight (if primary? 600 500) :focus-policy tab-focus
                                  :underline-shortcut (underline? action (if primary? "C" "S"))
                                  :tooltip (if primary?
                                             (str "Copy image to clipboard and close (" (hint action) ")")
                                             (str "Save under " pictures-root "/YEAR/MONTH and close (" (hint action) ")"))
                                  :sensitive (toolbar-enabled? action) :on-click (action! action)}])]
    [:vbox {:key "header" :id "editor-header" :hexpand true :color "#1b1e23"
            :padding 12 :padding-left 16 :padding-right 16 :spacing 12}
     (when arranging?
       [:hbox {:id "arrangement-actions" :hexpand true :spacing 12}
        [:label {:label "Snitt" :font-size 20 :font-weight 600}]
        [:spacer {:hexpand true}]
        [:action-button {:id "save-arrangement" :label "Save and close"
                         :shortcut-label true
                         :underline-shortcut (underline? "saveClose" "S")
                         :tooltip (str "Save under " pictures-root "/YEAR/MONTH and close (" (hint "saveClose") ")")
                         :sensitive (enabled? "saveClose") :on-click #(dispatch! "saveClose")}]
        [:action-button {:id "copy-arrangement" :label "Copy and close"
                         :shortcut-label true
                         :underline-shortcut (underline? "copyClose" "C")
                         :tooltip (str "Copy the combined image and close (" (hint "copyClose") ")")
                         :sensitive (enabled? "copyClose") :on-click #(dispatch! "copyClose")}]
        [:action-button {:id "arrange" :label "Annotate →" :appearance "primary" :font-weight 600
                         :natural-height true :font-size 14
                         :tooltip (str "Continue to annotate (" (hint "arrange") ")")
                         :sensitive (enabled? "arrange") :on-click #(dispatch! "arrange")}]])
     (when (pos? region-count)
       [:vbox {:id "arrangement-phase" :hexpand true :spacing 6}
        [:hbox {:spacing 12}
         [:label {:label "Arrange" :font-size 18 :font-weight (if arranging? 700 400)
                  :color (if arranging? "#91bff0" "#9ba5b5")}]
         [:label {:label "→" :color "#9ba5b5"}]
         [:label {:label "Annotate (optional)" :font-size 18 :font-weight (if arranging? 400 700)
                  :color (if arranging? "#9ba5b5" "#91bff0")}]]
        [:label {:label (if arranging? "Put your captures in order and choose a layout."
                           "Add marks or remove content from the combined image.")
                 :hexpand true :color "#b8c1ce"}]])
     (if arranging?
       [:flow {:id "arrangement-toolbar" :hexpand true :spacing 4}
         [:label {:label (str region-count " regions") :fixed-height 40 :font-weight 600
                  :color "#dfe5ed" :padding-right 12}]
         [:label {:label "Columns" :fixed-height 40 :color "#9ba5b5"}]
         [:action-button {:id "columns-less" :label "−" :accessible-name "Fewer columns"
                          :tooltip (str "Fewer columns (" (hint "columnsLess") ")")
                          :sensitive (enabled? "columnsLess") :on-click #(dispatch! "columnsLess")}]
         [:label {:label (str columns) :fixed-height 40 :color "#dfe5ed"}]
         [:action-button {:id "columns-more" :label "+" :accessible-name "More columns"
                          :tooltip (str "More columns (" (hint "columnsMore") ")")
                          :sensitive (enabled? "columnsMore") :on-click #(dispatch! "columnsMore")}]
         (for [[id label action icon] [["region-left" "Move region left" "moveRegionLeft" "arrow-left"]
                                       ["region-right" "Move region right" "moveRegionRight" "arrow-right"]
                                       ["region-remove" "Remove region" "removeRegion" "trash"]]]
           [:action-button {:key id :id id :icon (str "qrc:/icons/" icon ".svg")
                            :icon-only true :accessible-name label
                            :tooltip (str label " (" (hint action) ")")
                            :sensitive (enabled? action) :on-click #(dispatch! action)}])]
       [:toolbar {:id "annotation-toolbar" :hexpand true}
        [:hbox {:id "annotation-tool-cluster" :spacing 6}
         [:hbox {:id "annotation-tools" :spacing 2}
          (for [[action name label glyph] annotation-tools]
            [:vbox {:key name :spacing 4 :preferred-width 40 :max-width 40}
             [:action-button {:id (if (#{"rect" "arrow"} name) name (str "tool-" name))
                              :icon (str "qrc:/icons/annotation/" glyph ".svg") :icon-only true
                              :preserve-icon-colors true :opacity (if (toolbar-enabled? action) 1 0.4)
                              :accessible-name label :focus-policy tab-focus
                              :tooltip (str (if (= name "cut") "Cut: remove an image strip" label) " (" (hint action) ")")
                              :checked (= tool name) :preferred-width 40 :preferred-height 40
                              :sensitive (toolbar-enabled? action) :on-click (action! action)}]
             (assoc-in (caption (str "shortcut-" name) (hint action)) [1 :elide] (value/enum "ElideRight"))])]
         [:hbox {:id "history-buttons" :spacing 2 :margin-left 10 :alignment top}
          (for [[id label action icon] [["undo" "Undo" "undo" "undo-2"] ["redo" "Redo" "redo" "redo-2"]]]
            [:action-button {:key id :id id :icon (str "qrc:/icons/" icon ".svg") :icon-only true
                             :accessible-name label :focus-policy tab-focus
                             :tooltip (str label " (" (hint action) ")")
                             :sensitive (enabled? action) :on-click (action! action)}])]]
        [:vbox {:id "mode-group" :spacing 4}
         [:hbox {:id "mode-segmented-control" :color "#22262c" :radius 7 :border-width 1 :border-color "#424952"
                 :selected-segment (if (= color-mode "good") 0 1) :segment-color (mode-color (or color-mode "bad"))
                 :preferred-width 168 :preferred-height 40 :padding 1 :spacing 0 :alignment center}
          (for [[action mode label] [["goodMode" "good" "Good"] ["badMode" "bad" "Bad"]]]
            [:action-button {:key mode :id (str "ink-" mode) :label label :font-size 12 :appearance "segment"
                             :mode-color (mode-color mode) :focus-policy tab-focus
                             :underline-shortcut (underline? action (if (= mode "good") "G" "B"))
                             :hexpand true :vexpand true :checked (= color-mode mode)
                             :accessible-name (str label " mode") :tooltip (str label " mode (" (hint action) ")")
                             :sensitive (toolbar-enabled? action) :on-click (action! action)}])]
         (caption "mode-heading" "Color Mode")]
        [:label {:id "annotation-hint" :label (if (seq notice) notice hint-text)
                 :color (if (seq notice) "#91bff0" "#8f99a8") :font-size 12
                 :wrap (value/enum "WordWrap")}]
        [:vbox {:id "finish-group" :spacing 4}
         [:hbox {:id "finish-buttons" :spacing 4 :alignment center}
          (finish "save" "Save" "saveClose" false)
          (finish "copy" "Copy" "copyClose" true)]
         (caption "finish-heading" "Finish (will close)")]])]))

(defn empty-editor [hints dispatch!]
  (let [hint #(get hints % "")
        action (fn [id text action primary?]
                 [:action-button {:id id :label (str text " (" (hint action) ")")
                                  :alignment center :appearance (if primary? "primary" "action")
                                  :font-size (if primary? 14 13) :natural-height primary?
                                  :font-weight (if primary? 600 500)
                                  :tooltip (str (get {"capture" "Capture a screen region"
                                                     "capture-multiple" "Capture multiple regions"
                                                     "record" "Record a region"} id text)
                                                " (" (hint action) ")")
                                  :on-click #(dispatch! action)}])]
    [:center {:id "empty-editor"}
     [:vbox {:spacing 16}
      [:label {:id "empty-heading" :label "No screenshot open" :font-size 26 :font-weight 600 :alignment center}]
      [:label {:id "empty-hint" :label (str "Press " (hint "globalCapture") " to select a screen region or stop recording.")
               :wrap (value/enum "NoWrap")
               :color "#9ba5b5" :font-size 14 :alignment center}]
      (action "capture" "Capture region" "capture" true)
      [:hbox {:alignment center :spacing 5}
       (action "open" "Open image" "openImage" false)
       [:label {:label "or" :color "#9ba5b5"}]
       (action "paste" "Paste image" "pasteImage" false)]
      (action "capture-multiple" "Multiple regions" "captureMultiple" false)
      (action "record" "Record region" "captureVideo" false)]]))

(defn footer [{:keys [arranging? notice hints scale width height]}]
  [:hbox {:key "footer" :id "editor-footer" :hexpand true :preferred-height 36
          :color "#1b1e23" :padding-left 16 :padding-right 16 :spacing 12}
   (if arranging?
     [:label {:label (if (seq notice) notice
                      (str "Drag to reorder · " (get hints "columnsMore") "/" (get hints "columnsLess")
                           " columns · " (get hints "arrange") " to annotate"))
              :hexpand true :font-size 12 :color (if (seq notice) "#91bff0" "#8f99a8")
              :elide (value/enum "ElideRight")}]
     [:spacer {:hexpand true}])
   [:label {:id "preview-status" :label (str "Preview " (Math/round (* scale 100)) "% · Image " width " × " height " px")
            :color "#8f99a8" :font-size 12
            :tooltip-on-press true
            :tooltip "100% shows one image pixel per display pixel; larger images fit the viewing area"}]])
