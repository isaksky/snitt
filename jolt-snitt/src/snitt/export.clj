(ns snitt.export
  "Screenshot destination, scale, and completion policy for the Jolt editor."
  (:require [clojure.string :as str]
            [glimmer-qt.object :as object]))

(defn scale [{:strs [annotations width height sourceWidth sourceHeight]}]
  (if (or (not annotations) (not (number? width)) (not (number? height))
          (not (pos? width)) (not (pos? height)))
    1.0
    (let [shorter (min width height)]
      (if (>= shorter 720)
        1.0
        (max 1.0 (min 3.0 (/ 720.0 shorter)
                      (/ 16384.0 (max sourceWidth sourceHeight))
                      (Math/sqrt (/ (* 32.0 1024 1024)
                                    (max 1.0 (* (double sourceWidth) sourceHeight))))))))))

(defn path [root {:strs [year month time token]}]
  (when (and (string? root) (seq root) (string? year) (seq year)
             (integer? month) (<= 1 month 12) (string? time) (seq time)
             (string? token) (seq token))
    (str (str/replace root #"[/\\]+$" "") "/" year "/" month
         "/snitt-" time "-" token ".png")))

(defn copy! [canvas]
  (let [facts (object/invoke! canvas "exportFacts()" [])]
    (if-let [error (get facts "error")]
      {:ok? false :error error}
      (let [result (object/invoke! canvas "copyDocumentRender(double)" [(double (scale facts))])]
        {:ok? (true? (get result "ok")) :error (get result "error")}))))

(defn save! [canvas root]
  (if (or (not (string? root)) (empty? root))
    {:ok? false :error "There is no screenshot or Pictures folder to save to."}
    (let [facts (object/invoke! canvas "exportFacts()" [])
          factor (double (scale facts))]
      (if-let [error (get facts "error")]
        {:ok? false :error error}
      (loop [attempt 0]
        (if (= attempt 5)
          {:ok? false :error (str "Could not find a unique screenshot filename in " root ".")}
          (let [candidate (path root (object/invoke! canvas "saveStamp()" []))]
            (if-not candidate
              {:ok? false :error "Could not choose a screenshot filename."}
              (let [result (object/invoke! canvas "saveDocumentPng(QString,double)"
                                           [candidate factor])]
                (case (get result "status")
                  "saved" {:ok? true :path (get result "path")}
                  "collision" (recur (inc attempt))
                  {:ok? false :error (or (get result "error")
                                         "Could not save the screenshot.")}))))))))))
