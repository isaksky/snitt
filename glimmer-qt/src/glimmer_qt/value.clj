(ns glimmer-qt.value
  "Explicit Qt values. Plain nil/boolean/string/int64/double/vector/string-key map
  values marshal recursively. Qt-specific values use Value, never magic maps."
  (:require [clojure.edn :as edn]
            [clojure.string :as str]))

(defrecord Value [type value])
(defn color [s] (->Value :color s))
(defn url [s] (->Value :url s))
(defn local-file [path] (->Value :file path))
(defn point [x y] (->Value :point [x y]))
(defn size [width height] (->Value :size [width height]))
(defn rect [x y width height] (->Value :rect [x y width height]))
(defn point-f [x y] (->Value :point-f [x y]))
(defn size-f [width height] (->Value :size-f [width height]))
(defn rect-f [x y width height] (->Value :rect-f [x y width height]))
(defn enum [key] (->Value :enum [key]))
(defn flags [& keys] (->Value :flags (vec keys)))
(defn object [handle] (->Value :object handle))

(defn- invalid! [value]
  (throw (ex-info "Unsupported or invalid Qt value." {:value value})))
(defn- finite? [x]
  (and (number? x) (not (Double/isNaN (double x))) (not (Double/isInfinite (double x)))))
(declare wire)
(defn- typed-wire [{:keys [type value] :as v}]
  (let [tag (name type)]
    (cond
      (#{:color :url :file} type)
      (if (string? value) [tag value] (invalid! v))
      (= :object type)
      (if (and (integer? value) (<= 1 value 9223372036854775807)) [tag (str value)] (invalid! v))
      (#{:enum :flags} type)
      (if (and (vector? value) (every? string? value)
               (every? #(boolean (re-matches #"[A-Za-z_][A-Za-z_0-9]*" %)) value)
               (or (= type :flags) (= 1 (count value))))
        [tag value] (invalid! v))
      (#{:point :size :rect :point-f :size-f :rect-f} type)
      (let [integral? (#{:point :size :rect} type)
            n (if (#{:rect :rect-f} type) 4 2)]
        (if (and (vector? value) (= n (count value))
                 (every? #(if integral? (and (integer? %) (<= -2147483648 % 2147483647)) (finite? %)) value))
          [tag (mapv #(str (if integral? % (double %))) value)] (invalid! v)))
      :else (invalid! v))))
(defn wire [v]
  (cond
    (instance? Value v) (typed-wire v)
    (nil? v) ["nil" ""]
    (boolean? v) ["bool" (if v "1" "0")]
    (string? v) ["string" v]
    (integer? v) (if (<= -9223372036854775808 v 9223372036854775807) ["int" (str v)] (invalid! v))
    (float? v) (if (finite? v) ["double" (str v)] (invalid! v))
    (vector? v) ["list" (mapv wire v)]
    (map? v) (if (every? string? (keys v)) ["map" (mapv (fn [[k x]] [k (wire x)]) v)] (invalid! v))
    :else (invalid! v)))

;; The wire format deliberately uses only arrays and strings: the JSON/EDN
;; common subset. Numeric payloads are decimal strings, never JSON doubles.
(def ^:private json-escapes
  (merge {\\ "\\\\" \" "\\\""}
         (into {} (map (fn [n] [(char n) (format "\\u%04x" n)]) (range 32)))))
(defn- json [v]
  (if (string? v) (str "\"" (str/escape v json-escapes) "\"")
      (str "[" (str/join "," (map json v)) "]")))
(defn encode [v] (json (wire v)))
(defn- unwire [[tag payload]]
  (case tag
    "nil" nil
    "bool" (= payload "1")
    "string" payload
    "int" (parse-long payload)
    "double" (parse-double payload)
    "list" (mapv unwire payload)
    "map" (into {} (map (fn [[k v]] [k (unwire v)]) payload))
    "object" (object (parse-long payload))
    (let [type (keyword tag)]
      (->Value type (if (#{:point :size :rect :point-f :size-f :rect-f} type)
                     (mapv (if (#{:point :size :rect} type) parse-long parse-double) payload)
                     payload)))))
(defn decode [s] (unwire (edn/read-string s)))
