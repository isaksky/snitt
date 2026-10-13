(ns snitt.trim-test
  (:require [clojure.string :as str]
            [clojure.test :refer [deftest is run-tests]]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.native :as native]
            [snitt.trim :as trim]))

(deftest parsing-and-generation
  (is (= ["out_time_" nil] (trim/progress-lines "" "out_time_" 1000)))
  (is (= ["" 0.5] (trim/progress-lines "out_time_" "us=500000\n" 1000)))
  (is (= ["" 1.0] (trim/progress-lines "" "out_time_us=9000000\n" 1000)))
  (is (= ["" nil] (trim/progress-lines "" "out_time_us=N/A\n" 1000)))
  (is (not (trim/valid-clip? {})))
  (is (trim/valid-duration? 901 1000))
  (is (not (trim/valid-duration? 1003 1000)))
  (let [owner {:state (atom (assoc (trim/initial) :jobs {"export" 2}))}
        before @(:state owner)]
    (trim/event! owner "export" 1 true "" "")
    (is (= before @(:state owner)))))

(def completed (atom false))
(defn check! [v message] (when-not v (throw (ex-info message {}))))

;; Fault-injection programs: a silent success, a failure exit, and a long run.
(defn helper [kind]
  (if (str/includes? (System/getProperty "os.name") "Windows")
    (let [system (str (System/getenv "SystemRoot") "\\System32\\")]
      (case kind
        :true [(str system "cmd.exe") ["/c" "exit 0"]]
        :false [(str system "cmd.exe") ["/c" "exit 1"]]
        :sleep [(str system "PING.EXE") ["-n" "31" "127.0.0.1"]]))
    (case kind
      :true ["/usr/bin/true" []]
      :false ["/usr/bin/false" []]
      :sleep ["/bin/sleep" ["30"]])))
(defn wait! [label pred action n]
  (qt/later! 20
    #(cond (pred) (action)
           (zero? n) (throw (ex-info (str "Timeout: " label) {}))
           :else (wait! label pred action (dec n)))))
(defn thumbnails? [owner]
  (let [urls (trim/property owner "thumbnails")]
    (and (= 12 (count urls)) (every? seq urls))))
(defn run-case! [path mode start end]
  (native/install!)
  (let [{:keys [root trim]} (native/create-services!)
        handle (:handle trim)
        fault (atom (if (= mode "retry") "replace-failure" mode))
        finalized (atom false)
        progress (atom [])
        calls (atom [])
        finish (fn []
                 (check! (= 1 (q/gui-thread)) "Completion runs on Qt GUI thread")
                 (widget/dispose! root)
                 (reset! completed true)
                 (qt/quit!))]
    (reset! (:invoke trim)
      (fn [target signature args]
        (swap! calls conj [signature args])
        (cond
          (and (= @fault "replace-failure") (= signature "replace(QString,QString)"))
          (object/invoke! target signature [(first args) (str path ".locked")])
          (and (= signature "start(QString,int,QString,QStringList)")
               (= (first args) "probe") (= @fault "probe-failure"))
          (object/invoke! target signature (into [(nth args 0) (nth args 1)] (helper :true)))
          (and (= signature "start(QString,int,QString,QStringList)")
               (= (first args) "export") (= @fault "process-failure"))
          (object/invoke! target signature [(nth args 0) (nth args 1) "/missing/snitt-ffmpeg" []])
          (and (= signature "start(QString,int,QString,QStringList)")
               (= (first args) "export") (= @fault "exit-failure"))
          (object/invoke! target signature (into [(nth args 0) (nth args 1)] (helper :false)))
          (and (= signature "start(QString,int,QString,QStringList)")
               (= (first args) "probe") (= @fault "probe-start-failure"))
          (object/invoke! target signature [(nth args 0) (nth args 1) "/missing/snitt-ffprobe" []])
          (and (= signature "start(QString,int,QString,QStringList)")
               (= (first args) "export") (contains? #{"cancel-running" "dispose"} @fault))
          (object/invoke! target signature (into [(nth args 0) (nth args 1)] (helper :sleep)))
          :else (object/invoke! target signature args))))
    (object/subscribe! root handle "finalized(QString)" (fn [_] (reset! finalized true)))
    (object/subscribe! root handle "changed()"
      #(do (check! (= 1 (q/gui-thread)) "Progress/error runs on Qt GUI thread")
           (when (trim/property trim "busy") (swap! progress conj (trim/property trim "progress")))
           (when (and (= @fault "cancel-probe") (= :validating (:phase @(:state trim))))
             (reset! fault "canceled-probe") (trim/cancel! trim))))
    (trim/open! trim path)
    (wait! "source metadata" #(pos? (trim/property trim "duration"))
      (fn []
        (cond
          (= mode "thumbnails")
          (do (trim/window! trim (max 0 (- (trim/property trim "duration") 100)) (trim/property trim "duration"))
              (wait! "EOF thumbnails" #(thumbnails? trim)
                #(let [old (first (trim/property trim "thumbnails"))
                       old-jobs (:jobs @(:state trim))]
                   (check! (empty? (trim/property trim "problem")) "EOF fallback succeeds")
                   (trim/window! trim 0 (trim/property trim "duration"))
                   (trim/window! trim 0 (max 1 (quot (trim/property trim "duration") 2)))
                   (doseq [[channel generation] old-jobs] (trim/event! trim channel generation true "" ""))
                   (wait! "new thumbnail generation" #(thumbnails? trim)
                     (fn []
                       (check! (not= old (first (trim/property trim "thumbnails"))) "New window owns new thumbnail directory")
                       (check! (empty? (trim/property trim "problem")) "Window refresh succeeds")
                       (trim/keep! trim)
                       (check! (zero? (object/invoke! handle "activeJobs()" [])) "No worker after keep")
                       (finish)) 1500)) 1500))
          (= mode "dispose")
          (do (trim/export! trim start end) (qt/later! 80 finish))
          :else
          (do
            (trim/export! trim start end)
            (when (= mode "cancel")
              (let [generation (get-in @(:state trim) [:jobs "export"])]
                (trim/cancel! trim)
                (trim/event! trim "export" generation true "" "")))
            (when (= mode "cancel-running") (qt/later! 80 #(trim/cancel! trim)))
            (wait! "export terminal" #(not (trim/property trim "busy"))
              (fn []
                (if (= mode "success")
                  (wait! "finalized" #(deref finalized)
                    #(do (check! (empty? (trim/property trim "path")) "Successful replacement clears session")
                         (check! (seq @progress) "Export progress delivered")
                         (check! (zero? (object/invoke! handle "activeJobs()" [])) "No workers during replacement")
                         (finish)) 100)
                  (do
                    (check! (= path (trim/property trim "path")) "Failed/canceled operation retains source")
                    (check! (not @finalized) "No failure finalization")
                    (check! (= 0.0 (trim/property trim "progress")) "Progress resets")
                    (when (= mode "cancel-probe") (check! (= @fault "canceled-probe") "Canceled validation worker"))
                    (when (contains? #{"replace-failure" "probe-failure" "process-failure" "exit-failure" "probe-start-failure" "retry"} mode)
                      (check! (seq (trim/property trim "problem")) "Failure diagnostic"))
                    (reset! fault "")
                    (wait! "resume after failure/cancel" #(thumbnails? trim)
                      #(if (= mode "retry")
                         (do (trim/export! trim start end)
                             (wait! "retry succeeds" (fn [] @finalized)
                               (fn [] (check! (empty? (trim/property trim "path")) "Retry clears source") (finish)) 1500))
                         (do (trim/keep! trim)
                             (check! (zero? (object/invoke! handle "activeJobs()" [])) "All preview file handles released")
                             (finish))) 1500)))) 1500)))) 300)))
(defn -main [& [path mode start end]]
  (let [{:keys [fail error]} (run-tests 'snitt.trim-test)]
    (when (pos? (+ fail error)) (System/exit 1)))
  (try
    (qt/run-application! #(run-case! path mode (Long/parseLong start) (Long/parseLong end))
      :app-id "snitt-jolt-trim-test" :keep-alive true :auto-quit-ms 60000)
    (check! @completed "Trim case completed")
    (check! (zero? (q/owned-count)) "Native handles released")
    (println (str "PASS: Jolt trim " mode " " path))
    (finally (shutdown-agents))))
