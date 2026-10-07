(ns snitt.document-test
  (:require [clojure.test :refer [deftest is run-tests]]
            [glimmer-qt.core :as qt]
            [glimmer-qt.object :as object]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as widget]
            [snitt.document :as document]
            [snitt.native :as native]))

(def checks (atom 0))
(defn check! [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {}))))

(deftest bounded-history-policy
  (let [root {:generation 1 :source 1 :position 0
              :history [{:image 1 :bytes 100 :commands []}]}
        append (fn [state id bytes]
                 (document/commit state {:kind :erase}
                                  {:image id :operation (+ 1000 id) :bytes bytes}))
        limited (reduce #(append %1 %2 100) root (range 2 70))
        large (append (append root 2 (* 200 1024 1024)) 3 (* 200 1024 1024))
        branch (append (assoc limited :position 0) 90 100)]
    (is (= 50 (count (:history limited))))
    (is (= 49 (:position limited)))
    (is (= 1 (count (:history large))))
    (is (not (document/can-undo? large)))
    (is (= 2 (count (:history branch))))
    (is (not (document/can-redo? branch)))
    (is (not (contains? (set (document/retained branch)) 69)))
    (is (= :erase (:kind (last (:commands (document/current branch))))))))

(def commands
  [{:kind :annotation :tool "rect" :start [7.5 8.25] :end [86.5 70.75] :stroke-width 3}
   {:kind :annotation :tool "highlight" :start [12.2 11.7] :end [78.3 62.6] :stroke-width 4}
   {:kind :text :start [4.5 9.25] :end [100.5 78.25] :text "A 日本\nedit" :font-size 14}
   {:kind :cut :vertical? true :from 10 :to 17}
   {:kind :blur :start [4.25 4.75] :end [45.7 43.25] :block-size 12}
   {:kind :cut :vertical? false :from 9 :to 14}
   {:kind :erase :start [58.8 51.3] :end [16.3 18.75]}
   {:kind :cut :vertical? true :from 8 :to 12}
   {:kind :annotation :tool "arrow" :start [3.4 7.6] :end [77.8 55.3] :stroke-width 3}
   {:kind :annotation :tool "highlight" :start [25.4 21.5] :end [66.8 62.1] :stroke-width 2}])

(defn reference! [canvas command]
  (if (= :text (:kind command))
    (let [[x y] (:start command) [right bottom] (:end command)]
      (object/invoke! canvas "addText(double,double,double,double,QString,int)"
                      [(double x) (double y) (double (- right x)) (double (- bottom y))
                       (:text command) (:font-size command)]))
    (check! (native/draw! canvas command) "Reference command accepted")))

(defn resources [canvas] (object/invoke! canvas "documentResources()" []))
(defn save! [canvas path scale preview]
  (check! (object/invoke! canvas "saveDocumentRender(QString,double,bool)"
                         [path (double scale) preview]) "Render saved"))
(defn compare-renders! [migrated reference directory label]
  (doseq [scale [1 1.25 1.8 2.5 3] preview [false true]]
    (let [suffix (str label "-" scale "-" preview ".png")]
      (save! migrated (str directory "/migrated-" suffix) scale preview)
      (save! reference (str directory "/reference-" suffix) scale preview))))

(defn exercise! [directory]
  (native/install!)
  (let [migrated (widget/create! :snitt/canvas {})
        reference (widget/create! :snitt/canvas {})
        source (str directory "/source.png")]
    (try
      (document/create! migrated)
      (doseq [canvas [migrated reference]]
        (check! (native/load-image! canvas source) "Alpha fixture loaded")
        (object/invoke! canvas "setInkMode(QString)" ["good"]))
      (doseq [[index command] (map-indexed vector commands)]
        (check! (document/edit! migrated command) "Jolt edit committed")
        (reference! reference command)
        (check! (= (inc index) (:position (document/snapshot migrated))) "Position owned by Jolt")
        (compare-renders! migrated reference directory (str "edit" index)))
      (let [snapshot (document/snapshot migrated)
            allocated (resources migrated)]
        (doseq [command [nil {}
                          {:kind :cut :vertical? true :from nil :to 10}
                          {:kind :cut :vertical? true :from 1.5 :to 10}
                          {:kind :blur :start [##NaN 2] :end [20 30] :block-size 12}
                          {:kind :cut :vertical? true :from 0 :to 10000}
                          {:kind :cut :vertical? false :from 12 :to 12}
                          {:kind :blur :start [999 999] :end [1000 1000] :block-size 12}
                          {:kind :erase :start [5 5] :end [5 5]}
                          {:kind :annotation :tool "other" :start [4 4] :end [30 30] :stroke-width 4}
                          {:kind :annotation :tool "rect" :start [4 4] :end [30 30] :stroke-width -1}
                          {:kind :text :start [0 0] :end [20 20] :text "  " :font-size 12}]]
          (check! (not (document/edit! migrated command)) "Invalid edit rejected"))
        (object/invoke! migrated "failNextDocumentEdit()" [])
        (check! (not (document/edit! migrated (first commands))) "Preparation failure rejected")
        (object/invoke! migrated "failNextDocumentProjection()" [])
        (check! (not (document/edit! migrated (first commands))) "Projection failure rejects prepared edit")
        (object/invoke! migrated "failNextDocumentProjection()" [])
        (check! (not (document/undo! migrated)) "Projection failure preserves undo position")
        (check! (= snapshot (document/snapshot migrated)) "Failed edits preserve all Jolt state")
        (check! (= allocated (resources migrated)) "Failed edits allocate no retained resources")
        ;; Old imperative APIs cannot mutate the transferred document.
        (object/invoke! migrated "undo()" [])
        (check! (not (object/invoke! migrated "drawExternalCut(bool,int,int)" [true 1 3]))
                "Native history owner is disabled")
        (compare-renders! migrated reference directory "failed"))
      (dotimes [_ 4] (document/undo! migrated) (native/undo! reference))
      (compare-renders! migrated reference directory "undo")
      (dotimes [_ 2] (document/redo! migrated) (native/redo! reference))
      (compare-renders! migrated reference directory "redo")
      (let [old (document/snapshot migrated)
            discarded (:image (last (:history old)))
            command {:kind :erase :start [3.3 4.4] :end [22.7 19.3]}]
        (check! (document/edit! migrated command) "New branch commits")
        (reference! reference command)
        (check! (not (document/can-redo? (document/snapshot migrated))) "Redo branch discarded")
        (check! (empty? (object/invoke! migrated "prepareDocument(int,QVariantMap)"
                                       [discarded {"kind" "cut" "vertical?" true "from" 1 "to" 2}]))
                "Discarded branch frame is released")
        (check! (= 9 (get (resources migrated) "operations")) "Discarded branch samples are released"))
      (compare-renders! migrated reference directory "branch")
      (dotimes [_ 55]
        (let [command {:kind :blur :start [1 1] :end [33 33] :block-size 8}]
          (check! (document/edit! migrated command) "Bounded edit accepted")
          (reference! reference command)))
      (check! (= 50 (count (:history (document/snapshot migrated)))) "History cap enforced")
      (check! (= 51 (get (resources migrated) "images")) "Only 50 frames and source remain native")
      (compare-renders! migrated reference directory "capped")
      (let [before (get (resources migrated) "sampleBytes")
            command {:kind :erase :start [1 1] :end [3 3]}]
        (dotimes [_ 3] (document/undo! migrated) (native/undo! reference))
        (document/edit! migrated command)
        (reference! reference command)
        (check! (< (get (resources migrated) "sampleBytes") before)
                "Discarded pixel grids release their native sample storage"))
      (compare-renders! migrated reference directory "sample-branch")
      (check! (native/load-image! migrated source) "Replacement loaded")
      (check! (zero? (:position (document/snapshot migrated))) "Source replacement resets history")
      (check! (= 1 (get (resources migrated) "images")) "Replacement releases previous image handles")
      (check! (zero? (get (resources migrated) "operations")) "Replacement releases samples")
      (document/dispose! migrated)
      (check! (not (document/owned? migrated)) "Disposed owner is forgotten")
      (finally (document/dispose! migrated)
               (widget/dispose! migrated) (widget/dispose! reference)))))

(defn byte-budget! [directory]
  (let [canvas (widget/create! :snitt/canvas {})]
    (try
      (document/create! canvas)
      (check! (native/load-image! canvas (str directory "/large.png")) "Large source loads")
      (dotimes [_ 20]
        (check! (document/edit! canvas {:kind :erase :start [1 1] :end [2 2]})
                "Large edit accepted"))
      (let [state (document/snapshot canvas)
            bytes (reduce + (map :bytes (:history state)))
            allocated (resources canvas)]
        (check! (= 16 (count (:history state))) "256 MiB evicts before 50 frames")
        (check! (<= bytes document/max-bytes) "History stays within byte budget")
        (check! (= 17 (get allocated "images")) "Evicted large images are released")
        (check! (<= (get allocated "bytes") (+ document/max-bytes (* 2048 2048 4)))
                "Native image accounting matches Jolt history plus source"))
      (finally (document/dispose! canvas) (widget/dispose! canvas)))))

(defn privacy! [directory kind]
  (let [a (widget/create! :snitt/canvas {}) b (widget/create! :snitt/canvas {})]
    (try
      (doseq [[canvas file] [[a "source.png"] [b "secret.png"]]]
        (document/create! canvas)
        (native/load-image! canvas (str directory "/" file))
        (document/edit! canvas {:kind :annotation :tool "rect" :start [2 2] :end [80 64] :stroke-width 3})
        (document/edit! canvas {:kind kind :start [0 0] :end [96 80] :block-size 16})
        (document/edit! canvas {:kind :cut :vertical? true :from 9 :to 18})
        (document/edit! canvas {:kind :cut :vertical? false :from 7 :to 16})
        (document/edit! canvas {:kind :annotation :tool "arrow" :start [3 4] :end [65 58] :stroke-width 2}))
      (compare-renders! a b directory (str "privacy-" (name kind)))
      (finally (doseq [canvas [a b]] (document/dispose! canvas) (widget/dispose! canvas))))))

(defn -main [& _]
  (let [{:keys [fail error]} (run-tests 'snitt.document-test)]
    (when (pos? (+ fail error)) (System/exit 1)))
  (qt/run-application!
   #(do (exercise! (System/getenv "SNITT_JOLT_DATA_DIR"))
        (privacy! (System/getenv "SNITT_JOLT_DATA_DIR") :blur)
        (privacy! (System/getenv "SNITT_JOLT_DATA_DIR") :erase)
        (byte-budget! (System/getenv "SNITT_JOLT_DATA_DIR"))
        (qt/quit!))
   :app-id "snitt-document-test" :auto-quit-ms 10000)
  (println "PASS:" @checks "document ownership, rendering and resource checks")
  (shutdown-agents))
