(ns glimmer-qt.media-test
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as ratom]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.media :as media]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]))

(def checks (atom 0))
(def first-frames (atom 0))
(def observations (atom {}))
(def completed (atom false))
(def show-player? (ratom/atom true))
(def label-state (ratom/atom "initial"))
(defn check! [v message]
  (swap! checks inc)
  (when-not v (throw (ex-info message {:state @observations :frames @first-frames}))))
(defn surface [] (widget/find-by-id "review"))
(defn root []
  [:vbox {:hexpand true :vexpand true}
   (when @show-player?
     [:video-player {:id "review" :hexpand true :vexpand true
                     :on-first-frame #(when (= 1 (swap! first-frames inc))
                                        (media/pause! (surface))
                                        (media/position! (surface) 0))}])
   [:label {:label @label-state}]])
(defn wait-for! [predicate deadline-ms action]
  (let [start (System/currentTimeMillis)]
    (letfn [(poll []
              (cond (predicate) (action)
                    (> (- (System/currentTimeMillis) start) deadline-ms)
                    (throw (ex-info "Media condition timed out" {:state (media/state (surface))
                                                                   :frames @first-frames}))
                    :else (qt/later! 40 poll)))]
      (poll))))
(defn begin! [clip]
  (let [s (surface)
        p (media/player s)
        a (media/audio-output s)
        v (media/video-output s)
        sink (media/video-sink s)
        elapsed (atom 0.0)]
    (check! (every? q/alive? [s p a v sink]) "Media objects and sink are owned")
    (check! (= (value/enum "StoppedState") (:playback (media/state s))) "Initial playback state")
    (media/mute! s true)
    (check! (:muted (media/state s)) "Priming mute")
    (doseq [property [:position :duration :playback :media :error]]
      (media/observe! s s property #(swap! observations assoc property %)))
    (media/source! s (value/local-file clip))
    (wait-for! #(pos? (:duration (media/state s))) 7000
      (fn []
        (check! (pos? (:duration (media/state s))) "Generated clip duration")
        (check! (contains? #{(value/enum "LoadedMedia") (value/enum "BufferedMedia")}
                          (:media (media/state s))) "Typed loaded media state")
        (media/play! s)
        (wait-for! #(pos? @first-frames) 7000
          (fn []
            (check! (= 1 @first-frames) "One owned first-frame event")
            (check! (= (value/enum "PausedState") (:playback (media/state s)))
                    "First frame can pause muted priming")
            (check! (zero? (:position (media/state s))) "Priming seeks back to start")
            (check! (pos? (first (:value (q/read-property sink "videoSize"))))
                    "Video sink receives visible-size frames")
            (media/mute! s false)
            (media/play! s)
            (let [clock (qt/every! s 40 #(swap! elapsed + (:delta-seconds %)))]
              (qt/later! 450
                (fn []
                  (check! (> @elapsed 0.2) "Elapsed-time playhead advances independently of sparse frames")
                  (check! (= (value/enum "PlayingState") (:playback (media/state s))) "Playing state")
                  (when-let [path (System/getenv "SNITT_MEDIA_SCREENSHOT")]
                    (q/save-window @widget/session path))
                  (media/pause! s)
                  (check! (= (value/enum "PausedState") (:playback (media/state s))) "Paused state")
                  (media/position! s 1500)
                  (media/rate! s 1.5)
                  (check! (= 1500 (:position (media/state s))) "Seek position")
                  (check! (= 1.5 (:rate (media/state s))) "Playback rate")
                  (check! (not (:muted (media/state s))) "Unmute after first frame")
                  (reset! label-state "rerendered")
                  (media/play! s)
                  (qt/later! 220
                    (fn []
                      (check! (= (value/enum "PlayingState") (:playback (media/state s))) "Resume state")
                      (check! (= 1.5 (:rate (media/state s))) "Reactive render preserves playback rate")
                      (check! (= p (media/player s)) "Reactive render preserves player identity")
                      (check! (integer? (:position @observations)) "Typed position notification")
                      (check! (= 1 @first-frames) "No raw-frame callback stream")
                      (qt/cancel! clock)
                      (media/clear! s)
                      (check! (= (value/url "") (:source (media/state s))) "Source cleared")
                      (check! (= (value/enum "StoppedState") (:playback (media/state s))) "Stop state")
                      (check! (= (value/enum "NoMedia") (:media (media/state s))) "Decoder released before replacement")
                      (check! (and (integer? (:duration @observations))
                                   (instance? glimmer_qt.value.Value (:playback @observations))
                                   (instance? glimmer_qt.value.Value (:media @observations)))
                              "Typed duration and state notifications")
                      (media/source! s (value/local-file (str clip ".missing")))
                      (wait-for! #(and (:error @observations)
                                       (not= (value/enum "NoError") (:error @observations))) 7000
                        (fn []
                          (check! (instance? glimmer_qt.value.Value (:error (media/state s)))
                                  "Typed media error")
                          (check! (= (:error @observations) (:error (media/state s)))
                                  "Typed error notification")
                          (check! (seq (:error-string (media/state s))) "Media error text")
                          (media/clear! s)
                          (media/source! s (value/local-file clip))
                          (media/play! s)
                          (wait-for! #(>= @first-frames 2) 7000
                            (fn []
                              (check! (= 2 @first-frames) "First frame rearms for replacement source")
                              (check! (= (value/enum "PlayingState") (:playback (media/state s)))
                                      "Replacement plays")
                              (reset! show-player? false)
                              (qt/later! 100
                                (fn []
                                  (check! (not-any? q/alive? [s p a v sink]) "Playing surface disposes all media handles")
                                  (check! (zero? (object/live-count)) "All media observers disconnect")
                                  (reset! show-player? true)
                                  (qt/later! 80
                                    (fn []
                                      (let [loading (surface)
                                            children [(media/player loading)
                                                      (media/audio-output loading)
                                                      (media/video-output loading)
                                                      (media/video-sink loading)]]
                                        (media/source! loading (value/local-file clip))
                                        (check! (= (value/enum "LoadingMedia") (:media (media/state loading)))
                                                "Source enters loading state")
                                        (reset! show-player? false)
                                        (qt/later! 100
                                          (fn []
                                            (check! (not-any? q/alive? (cons loading children))
                                                    "Loading surface disposes all media handles")
                                            (reset! completed true)
                                            (qt/quit!)))))))))))))))))))))))

(defn -main [& [clip]]
  (when-not clip (throw (ex-info "Pass a generated clip path." {})))
  (try
    (ui/run root :width 480 :height 300 :title "Qt media verification"
            :on-ready #(qt/later! 80 (fn [] (begin! clip)))
            :auto-quit-ms 20000)
    (check! @completed "Media verification completed")
    (check! (zero? (q/owned-count)) "Session closes media objects")
    (println "PASS:" @checks "Qt multimedia checks")
    (finally (shutdown-agents))))
