(ns snitt.lifecycle
  "Startup and external command policy; Qt owns only path, lock and socket mechanics."
  (:require [clojure.string :as str]
            [glimmer-qt.object :as object]))

(defn normalize-arguments [absolute-path arguments]
  (mapv #(if (str/starts-with? % "-") % (absolute-path %)) arguments))

(defn command [arguments]
  (cond
    (some #{"--quit"} arguments) [:quit]
    (some #{"--background"} arguments) [:background]
    (some #{"--show"} arguments) [:show]
    :else (if-let [image (first (remove #(str/starts-with? % "-") arguments))]
            [:image image]
            [:capture])))

(defn startup-command [arguments]
  (if-let [image (first (remove #(str/starts-with? % "-") arguments))]
    [:image image]
    (cond
      (some #{"--quit"} arguments) [:quit]
      (some #{"--show"} arguments) [:show]
      (some #{"--background"} arguments) [:background]
      :else [:capture])))

(defn absolute-arguments [endpoint arguments]
  (normalize-arguments #(object/invoke! endpoint "absolutePath(QString)" [%]) arguments))
