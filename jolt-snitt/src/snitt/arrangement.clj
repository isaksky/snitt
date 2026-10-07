(ns snitt.arrangement
  "The Jolt editor's authoritative arrangement decisions. Captured pixels stay native.")

(defn captured [count]
  (when (<= 1 count 24)
    {:order (vec (range count)) :columns 2 :selected 0 :phase :arrange}))

(defn columns [arrangement requested]
  (when arrangement
    (assoc arrangement :columns (max 1 (min 6 (count (:order arrangement)) requested)))))

(defn select [arrangement index]
  (when (and arrangement (= :arrange (:phase arrangement))
             (<= 0 index) (< index (count (:order arrangement))))
    (assoc arrangement :selected index)))

(defn move [arrangement from to]
  (when (and arrangement (= :arrange (:phase arrangement))
             (<= 0 from) (< from (count (:order arrangement)))
             (<= 0 to) (< to (count (:order arrangement)))
             (not= from to))
    (let [order (:order arrangement)
          image (nth order from)
          without (vec (concat (subvec order 0 from) (subvec order (inc from))))]
      (assoc arrangement :order (vec (concat (subvec without 0 to) [image]
                                             (subvec without to))) :selected to))))

(defn remove-region [arrangement index]
  (when (and arrangement (= :arrange (:phase arrangement))
             (<= 0 index) (< index (count (:order arrangement))))
    (let [order (:order arrangement)
          remaining (vec (concat (subvec order 0 index) (subvec order (inc index))))]
      (when (seq remaining)
        (-> arrangement
            (assoc :order remaining :selected (min index (dec (count remaining))))
            (columns (:columns arrangement)))))))

(defn annotate [arrangement]
  (when (and arrangement (= :arrange (:phase arrangement)))
    (assoc arrangement :phase :annotate)))

(defn arrange [arrangement]
  (when (and arrangement (= :annotate (:phase arrangement)))
    (assoc arrangement :phase :arrange)))
