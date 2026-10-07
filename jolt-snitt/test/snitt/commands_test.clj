(ns snitt.commands-test
  (:require [clojure.test :refer [deftest is testing run-tests]]
            [snitt.commands :as commands]))

(def ready {:has-image? true :can-undo? true :can-redo? true
            :session-manager? true :columns 2 :region-count 3
            :selected-region 1})

(deftest editor-contexts
  (is (= [[:tool "rect"]] (commands/decide ready "toolRectangle")))
  (is (= [[:undo]] (commands/decide ready "undo")))
  (is (= [[:capture false true]] (commands/decide ready "captureVideo")))
  (is (= [[:finish :copy]] (commands/decide ready "copyClose")))
  (is (= [[:finish :save]] (commands/decide ready "saveClose")))
  (is (nil? (commands/decide ready "openImage")))
  (doseq [flag [:text-draft? :modal? :capturing? :recording? :review?]]
    (testing (name flag)
      (let [blocked (assoc ready flag true)]
        (doseq [action ["toolRectangle" "undo" "openImage" "pasteImage"
                        "capture" "saveClose" "dismiss"]]
          (is (nil? (commands/decide blocked action))))))))

(deftest arrangement-and-history
  (let [arranging (assoc ready :arranging? true)]
    (is (nil? (commands/decide arranging "toolRectangle")))
    (is (nil? (commands/decide arranging "undo")))
    (is (= [[:annotate]] (commands/decide arranging "arrange")))
    (is (= [[:columns 3]] (commands/decide arranging "columnsMore")))
    (is (= [[:move-region 1 0]] (commands/decide arranging "moveRegionLeft")))
    (is (= [[:remove-region 1]] (commands/decide arranging "removeRegion")))
    (is (= [[:open-dialog]] (commands/decide arranging "openImage")))
    (is (= [[:paste]] (commands/decide arranging "pasteImage")))
    (is (nil? (commands/decide (assoc arranging :selected-region 0) "moveRegionLeft")))
    (is (nil? (commands/decide (assoc arranging :columns 3) "columnsMore"))))
  (is (nil? (commands/decide (assoc ready :can-undo? false) "undo")))
  (is (nil? (commands/decide (assoc ready :can-redo? false) "redo")))
  (is (= [[:open-dialog]] (commands/decide {} "openImage")))
  (is (nil? (commands/decide {:has-image? true} "capture")))
  (is (false? (commands/enabled? ready "unknown"))))

(deftest review-context
  (let [review {:review? true :review-can-edit? true}]
    (is (= [[:review :play-pause]] (commands/decide review "reviewPlayPause")))
    (is (= [[:review :seek -5]] (commands/decide review "reviewSeekBackwardFar")))
    (is (= [[:review :mark-end]] (commands/decide review "reviewMarkEnd")))
    (is (= [[:review :cancel]] (commands/decide review "reviewCancel")))
    (is (nil? (commands/decide review "copyClose")))
    (is (nil? (commands/decide (assoc review :review-can-edit? false)
                               "reviewPlayPause")))
    (is (= [[:review :cancel]]
           (commands/decide (assoc review :review-can-edit? false) "reviewCancel")))
    (is (nil? (commands/decide (assoc review :modal? true) "reviewCancel")))))

(deftest global-commands
  (is (= [[:hotkey-capture]]
         (commands/global-decide {:session-manager? true} "globalCapture")))
  (is (= [[:show-editor]]
         (commands/global-decide {:session-manager? true} "showEditor")))
  (is (nil? (commands/global-decide {:session-manager? true :capture? true}
                                    "showEditor")))
  (is (nil? (commands/global-decide {} "globalCapture")))
  (is (nil? (commands/global-decide {:session-manager? true} "unknown"))))

(defn -main [& _]
  (let [{:keys [fail error]} (run-tests 'snitt.commands-test)]
    (System/exit (if (zero? (+ fail error)) 0 1))))
