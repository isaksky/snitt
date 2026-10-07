(ns hello.packaged
  "A small packaged Qt Quick and private playback fixture."
  (:require [glimmer.core :as ui]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.media :as media]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [hello.core :as hello]))

(defonce first-frame? (atom false))

(defn app []
  [:vbox {:hexpand true :vexpand true :spacing 12 :margin 20}
   [hello/app]
   [:video-player {:id "preview" :hexpand true :vexpand true
                   :on-first-frame #(do
                                      (reset! first-frame? true)
                                      (media/pause! (widget/find-by-id "preview")))}]])

(defn -main [& args]
  (let [[mode clip] (if (= "--smoke-test" (first args)) args (cons nil args))
        smoke? (= mode "--smoke-test")
        ready? (atom false)]
    (when (or (nil? clip) (seq (drop (if smoke? 2 1) args)))
      (throw (ex-info "Usage: GlimmerExperiment [--smoke-test] bundled-clip.mp4" {})))
    (reset! first-frame? false)
    (try
      (ui/run app :title "Glimmer Qt experiment" :width 600 :height 600
              :app-id "snitt.glimmer-experiment"
              :auto-quit-ms (when smoke? 12000)
              :on-ready #(let [surface (widget/find-by-id "preview")]
                           (when-not (and surface (widget/find-by-id "name"))
                             (throw (ex-info "Packaged Qt controls did not mount." {})))
                           (media/mute! surface true)
                           (media/source! surface (value/local-file clip))
                           (media/play! surface)
                           (when smoke?
                             (letfn [(poll []
                                       (cond
                                         @first-frame?
                                         (do
                                           (when-not (pos? (first (:value (q/read-property
                                                                           (media/video-sink surface)
                                                                           "videoSize"))))
                                             (throw (ex-info "Playback sink has no decoded frame." {})))
                                           (reset! ready? true)
                                           (qt/quit!))
                                         (not= (value/enum "NoError") (:error (media/state surface)))
                                         (throw (ex-info "Packaged playback failed."
                                                         {:error (:error-string (media/state surface))}))
                                         :else (qt/later! 50 poll)))]
                               (qt/later! 50 poll)))))
      (when smoke?
        (when-not @ready?
          (throw (ex-info "Packaged playback did not produce a frame in 12 seconds." {})))
        (println "PASS: packaged Qt controls and LGPL FFmpeg playback"))
      (finally (shutdown-agents)))))
