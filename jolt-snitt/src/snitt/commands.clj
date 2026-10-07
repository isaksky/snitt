(ns snitt.commands
  "Editor command policy. Inputs are a live session snapshot, never document data;
  returned effects are interpreted by the application on the Qt thread.")

(def tools
  {"toolCut" "cut" "toolRectangle" "rect" "toolHighlight" "highlight"
   "toolText" "text" "toolArrow" "arrow" "toolPixelate" "blur"
   "toolErase" "erase"})

(def editor-actions
  ["openImage" "pasteImage" "capture" "captureMultiple" "captureVideo"
   "toolCut" "toolRectangle" "toolHighlight" "toolText" "toolArrow"
   "toolPixelate" "toolErase" "goodMode" "badMode" "arrange"
   "columnsMore" "columnsLess" "removeRegion" "moveRegionLeft"
   "moveRegionRight" "undo" "redo" "copyClose" "saveClose" "dismiss"])

(defn enabled? [s action]
  (let [ordinary? (not (or (:text-draft? s) (:modal? s) (:capturing? s)
                           (:recording? s) (:review? s)))
        annotation? (and ordinary? (:has-image? s) (not (:arranging? s)))
        arrangement? (and ordinary? (:has-image? s) (:arranging? s))]
    (cond
      (contains? tools action) annotation?
      (#{"goodMode" "badMode"} action) annotation?
      (= action "returnArrange") (and annotation? (pos? (:region-count s 0)))
      (= action "undo") (and annotation? (:can-undo? s))
      (= action "redo") (and annotation? (:can-redo? s))
      (#{"openImage" "pasteImage"} action)
      (and ordinary? (or (not (:has-image? s)) (:arranging? s)))
      (#{"capture" "captureMultiple" "captureVideo"} action)
      (and ordinary? (or (:session-manager? s) (not (:has-image? s)) (:arranging? s)))
      (#{"arrange" "columnsMore" "columnsLess" "removeRegion"
          "moveRegionLeft" "moveRegionRight"} action)
      (and arrangement?
           (case action
             "columnsMore" (< (:columns s 1) (min 6 (:region-count s 0)))
             "columnsLess" (> (:columns s 1) 1)
             "removeRegion" (pos? (:region-count s 0))
             "moveRegionLeft" (pos? (:selected-region s -1))
             "moveRegionRight" (< (:selected-region s -1) (dec (:region-count s 0)))
             true))
      (#{"copyClose" "saveClose"} action) (and ordinary? (:has-image? s))
      (= action "dismiss") ordinary?
      (= action "reviewPlayPause") (and (:review? s) (:review-can-edit? s) (not (:modal? s)))
      (#{"reviewSeekBackward" "reviewSeekForward" "reviewSeekBackwardFar"
          "reviewSeekForwardFar" "reviewMarkStart" "reviewMarkEnd"} action)
      (and (:review? s) (:review-can-edit? s) (not (:modal? s)))
      (= action "reviewCancel") (and (:review? s) (not (:modal? s)))
      :else false)))

(defn decide [s action]
  (when (enabled? s action)
    (cond
      (contains? tools action) [[:tool (get tools action)]]
      (= action "goodMode") [[:ink "good"]]
      (= action "badMode") [[:ink "bad"]]
      (= action "openImage") [[:open-dialog]]
      (= action "pasteImage") [[:paste]]
      (= action "capture") [[:capture false false]]
      (= action "captureMultiple") [[:capture true false]]
      (= action "captureVideo") [[:capture false true]]
      (= action "arrange") [[:annotate]]
      (= action "returnArrange") [[:return-arrange]]
      (= action "columnsMore") [[:columns (inc (:columns s))]]
      (= action "columnsLess") [[:columns (dec (:columns s))]]
      (= action "removeRegion") [[:remove-region (:selected-region s)]]
      (= action "moveRegionLeft") [[:move-region (:selected-region s) (dec (:selected-region s))]]
      (= action "moveRegionRight") [[:move-region (:selected-region s) (inc (:selected-region s))]]
      (= action "undo") [[:undo]]
      (= action "redo") [[:redo]]
      (= action "copyClose") [[:finish :copy]]
      (= action "saveClose") [[:finish :save]]
      (= action "dismiss") [[:dismiss]]
      (= action "reviewPlayPause") [[:review :play-pause]]
      (= action "reviewSeekBackward") [[:review :seek -1]]
      (= action "reviewSeekForward") [[:review :seek 1]]
      (= action "reviewSeekBackwardFar") [[:review :seek -5]]
      (= action "reviewSeekForwardFar") [[:review :seek 5]]
      (= action "reviewMarkStart") [[:review :mark-start]]
      (= action "reviewMarkEnd") [[:review :mark-end]]
      (= action "reviewCancel") [[:review :cancel]])))

(defn global-decide [s action]
  (when (:session-manager? s)
    (case action
      "globalCapture" [[:hotkey-capture]]
      "showEditor" (when-not (:capture? s) [[:show-editor]])
      nil)))
