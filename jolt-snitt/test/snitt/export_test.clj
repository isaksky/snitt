(ns snitt.export-test
  (:require [clojure.test :refer [deftest is run-tests]]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.document :as document]
            [snitt.export :as export]
            [snitt.native :as native]))

(deftest scale-and-path-policy
  (is (= 1.0 (export/scale {"annotations" false "width" 80 "height" 60})))
  (is (= 3.0 (export/scale {"annotations" true "width" 80 "height" 60
                            "sourceWidth" 80 "sourceHeight" 60})))
  (is (= 1.0 (export/scale {"annotations" true "width" 800 "height" 800
                            "sourceWidth" 800 "sourceHeight" 800})))
  (is (= 1.0 (export/scale {"annotations" true "width" 80 "height" 60
                            "sourceWidth" 20000 "sourceHeight" 60})))
  (is (= 2.0 (export/scale {"annotations" true "width" 100 "height" 80
                            "sourceWidth" 8192 "sourceHeight" 100})))
  (is (= "/tmp/写真/2026/10/snitt-20261007-123456-001-abc.png"
         (export/path "/tmp/写真/" {"year" "2026" "month" 10
                                   "time" "20261007-123456-001" "token" "abc"}))))

(deftest collision-and-clipboard-results
  (let [calls (atom [])
        stamps (atom ["same" "different"])]
    (with-redefs [object/invoke! (fn [_ method args]
                                   (case method
                                     "exportFacts()" {"annotations" false "width" 80 "height" 60}
                                     "saveStamp()" (let [token (first @stamps)]
                                                     (swap! stamps rest)
                                                     {"year" "2026" "month" 10 "time" "t" "token" token})
                                     "saveDocumentPng(QString,double)"
                                     (do (swap! calls conj args)
                                         (if (= 1 (count @calls)) {"status" "collision"}
                                             {"status" "saved" "path" (first args)}))
                                     "copyDocumentRender(double)"
                                     {"ok" false "error" "Clipboard busy"}))]
      (let [result (export/save! :canvas "/tmp/写真")]
        (is (:ok? result))
        (is (= 2 (count @calls)))
        (is (not= (first (first @calls)) (first (second @calls))))
        (is (= (first (second @calls)) (:path result))))
      (is (= {:ok? false :error "Clipboard busy"} (export/copy! :canvas))))))

(defn- check! [value & [message]]
  (when-not value (throw (ex-info (or message "Export assertion failed") {}))))

(defn- exercise! [root]
  (native/install!)
  (let [canvas (widget/create! :snitt/canvas {})
        unicode (str root "/写真 👋")]
    (try
      (document/create! canvas)
      (check! (native/load-image! canvas (str root "/source.png")))
      (check! (document/edit! canvas {:kind :annotation :tool "rect"
                                  :start [2 3] :end [70 50] :stroke-width 3}))
      (check! (document/edit! canvas {:kind :cut :vertical? true :from 15 :to 25}))
      (check! (> (export/scale (object/invoke! canvas "exportFacts()" [])) 1.0))
      (let [first (export/save! canvas unicode)
            second (export/save! canvas unicode)]
        (check! (:ok? first) (:error first))
        (check! (:ok? second) (:error second))
        (check! (not= (:path first) (:path second)))
        (check! (.startsWith (:path first) unicode))
        (doseq [saved [first second]]
          (let [image (q/image-load @widget/session (:path saved))]
            (check! (pos? image))
            (q/image-dispose @widget/session image))))
      (let [blocked (str root "/blocked")]
        (spit blocked "occupied")
        (let [result (export/save! canvas blocked)]
          (check! (not (:ok? result)))
          (check! (.contains (:error result) "Could not create"))))
      (check! (:ok? (export/copy! canvas)))
      (let [image (q/clipboard-image @widget/session)]
        (check! (pos? image))
        (q/image-dispose @widget/session image))
      (check! (native/load-image! canvas (str root "/large.png")))
      (check! (document/edit! canvas {:kind :annotation :tool "rect"
                                  :start [2 2] :end [100 100] :stroke-width 3}))
      (check! (<= (export/scale (object/invoke! canvas "exportFacts()" [])) 1.0))
      (check! (:ok? (export/save! canvas (str root "/large-output"))))
      (finally (document/dispose! canvas) (widget/dispose! canvas)))))

(defn -main [& _]
  (let [{:keys [fail error]} (run-tests 'snitt.export-test)]
    (when (pos? (+ fail error)) (System/exit 1)))
  (qt/run-application!
   #(do (exercise! (System/getenv "SNITT_JOLT_DATA_DIR")) (qt/quit!))
   :app-id "snitt-export-test" :auto-quit-ms 10000)
  (shutdown-agents))
