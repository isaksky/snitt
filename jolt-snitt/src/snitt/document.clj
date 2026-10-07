(ns snitt.document
  "Authoritative immutable commands, current position and bounded image history.
  Pixels and prepared privacy samples are session-owned native handles; atoms
  contain only handles and metadata, never full image data."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]))

(def max-frames 50)
(def max-bytes (* 256 1024 1024))
(defonce ^:private owners (atom {}))

(defn empty-state [] {:generation nil :source 0 :history [] :position -1})
(defn current [state] (get (:history state) (:position state)))
(defn can-undo? [state] (pos? (:position state)))
(defn can-redo? [state] (< (inc (:position state)) (count (:history state))))

(defn commit
  "Pure history transition. Call only after native preparation succeeds."
  [state command {:keys [image operation bytes]}]
  (let [commands (conj (:commands (current state)) (assoc command :operation operation))
        frames (conj (into [] (take (inc (:position state)) (:history state)))
                     {:image image :bytes bytes :commands commands})]
    (loop [history frames total (reduce + (map :bytes frames))]
      (if (and (> (count history) 1)
               (or (> total max-bytes) (> (count history) max-frames)))
        (recur (vec (rest history)) (- total (:bytes (first history))))
        (assoc state :history history :position (dec (count history)))))))

(defn retained [state]
  (vec (into (if (pos? (:source state)) #{(:source state)} #{})
             (mapcat (fn [{:keys [image commands]}]
                       (cons image (map :operation commands))) (:history state)))))

(defn- collect! [canvas state]
  (object/invoke! canvas "retainDocument(QVariantList)" [(retained state)]))

(defn sync!
  "Observe source replacements synchronously, including before queued signals.
  A failed load never changes this generation or discards an existing branch."
  [canvas]
  (when-let [{:keys [state]} (get @owners canvas)]
    (let [{:strs [generation image bytes]} (object/invoke! canvas "documentSource()" [])]
      (when (and generation (not= generation (:generation @state)))
        (let [next {:generation generation :source image
                    :history (if (pos? image) [{:image image :bytes bytes :commands []}] [])
                    :position (if (pos? image) 0 -1)}]
          (reset! state next)
          (collect! canvas next))))
    @state))

(defn create! [canvas]
  (when (get @owners canvas) (throw (ex-info "Document already owned" {})))
  (object/invoke! canvas "enableDocument()" [])
  (let [state (atom (empty-state))]
    (swap! owners assoc canvas {:state state})
    (let [connection (object/subscribe! canvas canvas "documentSourceChanged()" #(sync! canvas))]
      (swap! owners assoc-in [canvas :connection] connection))
    (sync! canvas)
    state))

(defn owned? [canvas] (contains? @owners canvas))
(defn snapshot [canvas] (sync! canvas))

(defn dispose! [canvas]
  (when-let [{:keys [state connection]} (get @owners canvas)]
    (swap! owners dissoc canvas)
    (when connection (object/disconnect! connection))
    (when (q/alive? canvas) (collect! canvas (empty-state)))
    (reset! state (empty-state))))

(defn- project! [canvas state]
  (object/invoke! canvas "projectDocumentBatch(int,int,QVariantList,bool,bool)"
                  [(:source state) (:image (current state))
                   (mapv :operation (:commands (current state)))
                   (can-undo? state) (can-redo? state)]))

(defn- finite? [x]
  (and (number? x) (not (Double/isNaN (double x)))
       (not (Double/isInfinite (double x)))))
(defn- point? [point]
  (and (vector? point) (= 2 (count point)) (every? finite? point)))
(defn- int32? [x] (and (integer? x) (<= -2147483648 x 2147483647)))
(defn valid-command? [{:keys [kind start end vertical? from to tool stroke-width
                             block-size text font-size]}]
  (case kind
    :cut (and (boolean? vertical?) (int32? from) (int32? to))
    :blur (and (point? start) (point? end) (int32? block-size))
    :erase (and (point? start) (point? end))
    :annotation (and (point? start) (point? end) (#{"rect" "arrow" "highlight"} tool)
                     (finite? stroke-width) (pos? stroke-width))
    :text (and (point? start) (point? end) (string? text)
               (int32? font-size) (pos? font-size))
    false))

(defn edit! [canvas command]
  (when-let [before (sync! canvas)]
    (when (and (current before) (valid-command? command))
      (let [command (merge {:color (q/read-property canvas "ink")
                            :good-mode (= "good" (q/read-property canvas "colorMode"))}
                           (dissoc command :op))
            wire (into {} (map (fn [[k v]] [(name k) (if (keyword? v) (name v) v)]) command))
            prepared (object/invoke! canvas "prepareDocument(int,QVariantMap)"
                                     [(:image (current before)) wire])]
        (when (seq prepared)
          (try
            (let [next (commit before command (into {} (map (fn [[k v]] [(keyword k) v]) prepared)))]
              (when (project! canvas next)
                (reset! (:state (get @owners canvas)) next)
                true))
            (finally (collect! canvas @(:state (get @owners canvas))))))))))

(defn- move! [canvas delta]
  (when-let [before (sync! canvas)]
    (when ((if (neg? delta) can-undo? can-redo?) before)
      (let [next (update before :position + delta)]
        (when (project! canvas next)
          (reset! (:state (get @owners canvas)) next)
          true)))))

(defn undo! [canvas] (move! canvas -1))
(defn redo! [canvas] (move! canvas 1))
