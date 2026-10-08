(ns glimmer-qt.media
  "A small Qt Multimedia surface for review playback. All operations run on the
  GUI thread. The visual surface owns its player, audio output, video output and
  sink; child handles are borrowed and become stale with the surface."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]))

(widget/register! :video-player
  {:source "qrc:/glimmer-qt/video-player.qml"
   :props {:id ["objectName" :string ""]
           :fill-mode ["fillMode" :string "fit"]
           :anchor ["anchorMode" :string "none"]
           :hexpand ["Layout.fillWidth" :bool false]
           :vexpand ["Layout.fillHeight" :bool false]}
   :events {:on-first-frame :none}
   :init q/bind-first-frame})

(defn player [surface] (q/child-target surface "player"))
(defn audio-output [surface] (q/child-target surface "audioOutput"))
(defn video-output [surface] (q/child-target surface "videoOutput"))
(defn video-sink [surface] (q/child-target surface "videoSink"))

(defn source! [surface url]
  (when-not (and (instance? glimmer_qt.value.Value url)
                 (#{:url :file} (:type url)))
    (throw (ex-info "Media source must be value/url or value/local-file." {})))
  (q/set-property! (player surface) "source" url)
  nil)
(defn position! [surface ms]
  (when-not (and (integer? ms) (<= 0 ms 9223372036854775807))
    (throw (ex-info "Playback position must be nonnegative milliseconds." {})))
  (q/set-property! (player surface) "position" ms)
  nil)
(defn rate! [surface rate]
  (when-not (and (number? rate) (< 0 (double rate) 16))
    (throw (ex-info "Playback rate must be positive and below 16." {})))
  (q/set-property! (player surface) "playbackRate" (double rate))
  nil)
(defn mute! [surface muted?]
  (when-not (boolean? muted?) (throw (ex-info "Mute requires a boolean." {})))
  (q/set-property! (audio-output surface) "muted" muted?)
  nil)
(defn play! [surface] (object/invoke! (player surface) "play()" []) nil)
(defn pause! [surface] (object/invoke! (player surface) "pause()" []) nil)
(defn stop! [surface] (object/invoke! (player surface) "stop()" []) nil)
(defn clear! [surface]
  "Stop and clear the media source before replacing or trimming its file."
  (stop! surface)
  (source! surface (value/url "")))

(defn state [surface]
  "Read typed Qt enum states and errors without string or numeric guesses."
  (let [p (player surface)]
    {:source (q/read-property p "source")
     :position (q/read-property p "position")
     :duration (q/read-property p "duration")
     :playback (q/read-property p "playbackState")
     :media (q/read-property p "mediaStatus")
     :error (q/read-property p "error")
     :error-string (q/read-property p "errorString")
     :rate (q/read-property p "playbackRate")
     :muted (q/read-property (audio-output surface) "muted")}))

(defn observe! [owner surface property handler]
  "Observe :position, :duration, :playback, :media, :error, or :source.
  The handler receives the typed current Qt value. Disconnect with
  glimmer-qt.object/disconnect! or dispose the owner/surface."
  (let [name (case property
               :position "position" :duration "duration"
               :playback "playbackState" :media "mediaStatus"
               :error "error" :source "source"
               (throw (ex-info "Unsupported media observation." {:property property})))]
    (object/observe! owner (player surface) name handler)))
