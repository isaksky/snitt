(ns snitt.review-test
  (:require [snitt.trim :as trim]
            [clojure.test :refer [deftest is run-tests]]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.media :as media]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [jolt.ffi :as ffi]
            [snitt.native :as native]
            [snitt.review :as review]))

(ffi/defcfn click-native "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn key-native "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn slider-native "gqt_test_slider_input" [:uint64 :int :double] :int :blocking)

(deftest timeline-policy
  (let [short (review/initial 80)
        long (review/initial 4000)
        selected (-> long (review/set-handle :start 1.5)
                     (review/set-handle :end 2.5))]
    (is (= 0.08 (:end short)))
    (is (= 0.08 (:end (review/set-handle short :end 0))))
    (is (= 0.0 (:start (review/set-handle short :start 1))))
    (is (= 1.5 (:playhead (review/seek selected -5))))
    (is (= 2.5 (:playhead (review/seek selected 9))))
    (is (= 2.4 (:start (review/set-handle selected :start 4))))
    (is (= 1.6 (:end (review/set-handle selected :end 0))))
    (is (review/trimmed? selected))
    (is (not (review/trimmed? long)))
    (is (= 0.0 (:playhead (review/clock-step
                           (assoc long :playing true :interacting true) 0.4 1.0))))
    (is (= 0.4 (:playhead (review/clock-step
                           (assoc long :playing true :media (value/enum "BufferedMedia"))
                           0.4 1.0))))
    (is (= 0.0 (:playhead (review/clock-step
                           (assoc long :playing true :media (value/enum "StalledMedia"))
                           0.4 1.0))))))

(def finished (atom false))
(defn check! [condition message]
  (when-not condition (throw (ex-info message {}))))
(defn wait-for! [label predicate action attempts]
  (qt/later! 50
    #(cond (predicate) (action)
           (zero? attempts) (throw (ex-info (str "Timed out: " label) {}))
           :else (wait-for! label predicate action (dec attempts)))))

(defn check-load-failure! [owner trim video clip missing]
  (wait-for! "filmstrip thumbnail"
    #(seq (first (trim/property trim "thumbnails")))
    #(do
       (let [image (qt/find-by-id @(:window owner) "review-thumb-0")]
         (check! (= (value/url (first (trim/property trim "thumbnails")))
                    (q/read-property image "imageSource"))
                 "Glimmer filmstrip follows the native thumbnail"))
       (media/source! video (value/local-file missing))
       (wait-for! "playback failure" #(seq (:error @(:state owner)))
         #(do
            (check! (= clip (trim/property trim "path"))
                    "Playback failure retains saved media")
            (media/source! video (trim/property trim "source"))
            (wait-for! "playback recovery" #(:primed @(:state owner))
              #(review/command! owner :close) 120)) 120)) 180))

(defn finish-after-cancel! [owner trim video clip missing]
  (cond
    (= "1" (System/getenv "SNITT_REVIEW_CANCEL_BUSY"))
    (do
      (review/command! owner :export)
      (wait-for! "active export" #(trim/property trim "busy")
        #(do
           (review/command! owner :close)
           (check! (:dismiss-after-cancel @(:state owner))
                   "Close waits for active export cancellation")
           (wait-for! "active export canceled"
             #(empty? (trim/property trim "path"))
             #(check! (not (trim/property trim "busy"))
                      "Cancellation keeps saved media") 120)) 120))
    (= "1" (System/getenv "SNITT_REVIEW_EXPORT"))
    (do
      (review/command! owner :export)
      (check! (= (value/url "") (:source (media/state video)))
              "Export releases playback source")
      (wait-for! "export result"
        #(or (empty? (trim/property trim "path"))
             (seq (trim/property trim "problem")))
        #(check! (empty? (trim/property trim "problem"))
                 "Export replaced validated media") 240))
    :else (check-load-failure! owner trim video clip missing)))

(defn exercise! [clip missing]
  (native/install!)
  (let [services (native/create-services!)
        root (:root services)
        trim (:trim services)
        owner-ref (atom nil)]
    (trim/open! trim clip)
    (let [owner (review/open! root trim
                              #(do (reset! finished true)
                                   (review/dispose! @owner-ref)
                                   (widget/dispose! root)
                                   (qt/quit!)))
          _ (reset! owner-ref owner)]
      (wait-for! "review playback duration" #(pos? (:duration @(:state owner)))
        #(let [window @(:window owner)
               video (qt/find-by-id window "review-video")]
           (check! (and video (q/alive? video)) "Video surface mounted")
           (check! (q/read-property (qt/find-by-id window "review-save") "enabled")
                   "Short review can keep original")
           (wait-for! "first frame primed" #(:primed @(:state owner))
             #(do
                (wait-for! "priming pause" #(not (:priming @(:state owner)))
                  #(do
                     (check! (not (:muted (media/state video))) "Initial playback unmutes")
                     (check! (not (:playing @(:state owner))) "First frame pauses")
                     (when-let [path (System/getenv "SNITT_REVIEW_SCREENSHOT")]
                       (check! (q/save-window @widget/session path)
                               "Review screenshot saved"))
                     (let [start (qt/find-by-id window "review-start")
                           end (qt/find-by-id window "review-end")
                           seek (qt/find-by-id window "review-seek")]
                       (doseq [action [0 1 2]]
                         (check! (= 1 (slider-native start action 0.25)) "Actual start handle input"))
                       (check! (> (:start @(:state owner)) 0.04) "Start handle moved")
                       (doseq [action [0 1 2]]
                         (check! (= 1 (slider-native end action 0.75)) "Actual end handle input"))
                       (check! (< (:end @(:state owner)) (:duration @(:state owner)))
                               "End handle moved")
                       (doseq [action [0 1 2]]
                         (check! (= 1 (slider-native seek action 0.5)) "Actual playhead input"))
                       (check! (<= (:start @(:state owner)) (:playhead @(:state owner))
                                   (:end @(:state owner))) "Playhead stays inside trim")
                       (check! (not (:interacting @(:state owner))) "Scrub ends after release")
                       (review/handle! owner :start 0)
                       (review/handle! owner :end (:duration @(:state owner)))
                       (review/seek! owner 0))
                     (check! (= 1 (click-native (qt/find-by-id window "review-play")))
                             "Actual play button click")
                     (wait-for! "elapsed playhead"
                       #(> (:playhead @(:state owner))
                           (min 0.15 (/ (:duration @(:state owner)) 3)))
                       #(do
                          (check! (= 1 (key-native window 16777234 0)) "Actual left arrow")
                          (check! (< (:playhead @(:state owner)) 0.2) "Keyboard seek clamps")
                          (review/command! owner :play-pause)
                          (review/handle! owner :start 0.2)
                          (review/handle! owner :end 0.6)
                          (check! (review/trimmed? @(:state owner)) "Trim handles select range")
                          (review/command! owner :export)
                          (check! (:preparing @(:state owner)) "Export preparation begins")
                          (check! (= (value/url "") (:source (media/state video)))
                                  "Playback source released before replacement")
                          (review/command! owner :cancel)
                          (check! (not (:preparing @(:state owner))) "Preparation canceled")
                          (wait-for! "source restored"
                            #(not= (value/url "") (:source (media/state video)))
                            #(do
                               (check! (= clip (trim/property trim "path"))
                                       "Cancellation retains original media")
                               (finish-after-cancel! owner trim video clip missing))
                            100)) 120)) 100)) 100)) 120))))

(defn -main [& [clip missing]]
  (let [{:keys [fail error]} (run-tests 'snitt.review-test)]
    (when (pos? (+ fail error)) (System/exit 1)))
  (when clip
    (try
      (qt/run-application! #(exercise! clip missing)
                           :app-id "snitt-jolt-review-test" :keep-alive true
                           :auto-quit-ms 12000)
      (check! @finished "Review finished")
      (check! (zero? (widget/live-count)) "Review widgets released")
      (check! (zero? (q/owned-count)) "Review native handles released")
      (println "PASS: Jolt review playback, input, trim and cancellation")
      (finally (shutdown-agents)))))
