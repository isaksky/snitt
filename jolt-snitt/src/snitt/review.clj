(ns snitt.review
  "Jolt-owned recording review, using Qt Multimedia and Jolt trim operations."
  (:require [snitt.trim :as trim]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.media :as media]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [snitt.settings :as settings]))

(defn clamp [low high n] (max low (min high n)))
(defn minimum-range [duration] (min 0.1 duration))
(defn initial [duration]
  {:duration (/ (double (max 0 duration)) 1000.0) :start 0.0
   :end (/ (double (max 0 duration)) 1000.0) :playhead 0.0
   :interacting false :resume false :priming false :primed false
   :preparing false :dismiss-after-cancel false :playing false
   :media-duration 0 :media (value/enum "NoMedia") :error "" :busy false
   :problem "" :progress 0.0 :thumbnails []})
(defn editable? [s busy?]
  (and (pos? (:duration s)) (not busy?) (not (:preparing s))))
(defn trimmed? [{:keys [start end duration]}]
  (or (> start 0.001) (< end (- duration 0.001))))
(defn set-duration [s ms]
  (let [duration (/ (double (max 0 ms)) 1000.0)]
    (if (or (zero? duration) (= duration (:duration s))) s
      (assoc s :duration duration :end (if (pos? (:end s))
                                          (min (:end s) duration) duration)
               :start (min (:start s) duration)
               :playhead (min (:playhead s) duration)))))
(defn set-handle [s handle seconds]
  (let [t (clamp 0.0 (:duration s) (double seconds))
        gap (minimum-range (:duration s))]
    (case handle
      :start (let [start (min t (- (:end s) gap))]
               (assoc s :start start :playhead start))
      :end (let [end (max t (+ (:start s) gap))]
             (assoc s :end end :playhead end))
      s)))
(defn seek [s seconds]
  (assoc s :playhead (clamp (:start s) (:end s) (double seconds))))
(defn clock-step [s seconds rate]
  (if (or (not (:playing s)) (:priming s) (:interacting s)
          (contains? #{(value/enum "NoMedia") (value/enum "LoadingMedia")
                       (value/enum "StalledMedia") (value/enum "InvalidMedia")}
                     (:media s)))
    s
    (let [playhead (min (:end s) (+ (:playhead s) (* seconds rate)))]
      (assoc s :playhead playhead :playing (< playhead (:end s))))))
(defn fmt [seconds]
  (let [centiseconds (long (Math/round (* 100 (max 0 seconds))))]
    (format "%02d:%02d.%02d" (quot centiseconds 6000)
            (mod (quot centiseconds 100) 60) (mod centiseconds 100))))

(defn- surface [owner]
  (when-let [window @(:window owner)]
    (when (qt/window-alive? window)
      (let [handle (qt/find-by-id window "review-video")]
        (when (and handle (q/alive? handle)) handle)))))
(defn- busy? [owner] (trim/property (:trim owner) "busy"))
(defn- invoke-trim! [owner signature args]
  (when-not @(:closed owner) (trim/invoke! (:trim owner) signature args)))
(defn- position! [owner seconds]
  (when-let [video (surface owner)]
    (media/position! video (long (Math/round (* 1000 seconds))))))
(defn- finish-priming! [owner]
  (when (:priming @(:state owner))
    (when-let [token @(:prime-timer owner)] (qt/cancel! token))
    (reset! (:prime-timer owner) nil)
    (when-let [video (surface owner)]
      (media/pause! video)
      (position! owner (:start @(:state owner)))
      (media/mute! video false))
    (swap! (:state owner) assoc :priming false :playing false
           :playhead (:start @(:state owner)))))
(defn- prime! [owner]
  (when (and (not @(:closed owner)) (not (:primed @(:state owner)))
             (not (:preparing @(:state owner))) (surface owner))
    (swap! (:state owner) assoc :primed true :priming true)
    (let [video (surface owner)]
      (media/mute! video true)
      (position! owner (:start @(:state owner)))
      (media/play! video)
      (reset! (:prime-timer owner)
              (qt/later! video 500 #(finish-priming! owner))))))
(defn- pause-at-playhead! [owner]
  (when-let [video (surface owner)]
    (let [seconds (:playhead @(:state owner))]
      (media/pause! video)
      (position! owner seconds)
      (swap! (:state owner) assoc :playing false :playhead seconds))))
(defn seek! [owner seconds]
  (when (and (not @(:closed owner)) (editable? @(:state owner) (busy? owner)))
    (finish-priming! owner)
    (swap! (:state owner) seek seconds)
    (position! owner (:playhead @(:state owner)))))
(defn handle! [owner handle seconds]
  (when (and (not @(:closed owner)) (editable? @(:state owner) (busy? owner)))
    (finish-priming! owner)
    (swap! (:state owner) set-handle handle seconds)
    (position! owner (:playhead @(:state owner)))))
(defn interacting! [owner active?]
  (when (and (not @(:closed owner)) (editable? @(:state owner) (busy? owner)))
    (if active?
      (let [resume (and (not (:priming @(:state owner))) (:playing @(:state owner)))]
        (finish-priming! owner)
        (pause-at-playhead! owner)
        (swap! (:state owner) assoc :interacting true :resume resume))
      (let [resume (:resume @(:state owner))]
        (swap! (:state owner) assoc :interacting false :resume false)
        (when (and resume (< (:playhead @(:state owner)) (:end @(:state owner))))
          (when-let [video (surface owner)] (media/play! video))
          (swap! (:state owner) assoc :playing true))))))
(defn command! [owner action & args]
  (when (and owner (not @(:closed owner)))
    (let [s @(:state owner)]
      (case action
        :play-pause (when (editable? s (busy? owner))
                      (finish-priming! owner)
                      (if (:playing @(:state owner)) (pause-at-playhead! owner)
                        (let [s @(:state owner)
                              target (if (or (< (:playhead s) (:start s))
                                             (>= (:playhead s) (- (:end s) 0.01)))
                                       (:start s) (:playhead s))]
                          (seek! owner target)
                          (when-let [video (surface owner)] (media/play! video))
                          (swap! (:state owner) assoc :playing true))))
        :seek (seek! owner (+ (:playhead s) (first args)))
        :mark-start (handle! owner :start (:playhead s))
        :mark-end (handle! owner :end (:playhead s))
        :cancel (if (or (busy? owner) (:preparing s))
                  (do (when-let [token @(:export-timer owner)] (qt/cancel! token))
                      (reset! (:export-timer owner) nil)
                      (swap! (:state owner) assoc :preparing false)
                      (when (busy? owner) (invoke-trim! owner "cancelExport()" []))
                      (when (and (not (busy? owner))
                                 (seq (trim/property (:trim owner) "path")))
                        (when-let [video (surface owner)]
                          (media/source! video (trim/property (:trim owner) "source")))))
                  (invoke-trim! owner "keepOriginal()" []))
        :close (if (busy? owner)
                 (do (swap! (:state owner) assoc :dismiss-after-cancel true)
                     (invoke-trim! owner "cancelExport()" []))
                 (do (when-let [token @(:export-timer owner)] (qt/cancel! token))
                     (reset! (:export-timer owner) nil)
                     (swap! (:state owner) assoc :preparing false)
                     (invoke-trim! owner "keepOriginal()" [])))
        :keep (when-not (or (busy? owner) (:preparing s))
                (invoke-trim! owner "keepOriginal()" []))
        :export (when (and (editable? s (busy? owner)) (trimmed? s))
                  (swap! (:state owner) assoc :preparing true :playing false)
                  (when-let [video (surface owner)] (media/clear! video))
                  (reset! (:export-timer owner)
                          (qt/later! (surface owner) 180
                            #(when (and (not @(:closed owner))
                                        (:preparing @(:state owner)))
                               (reset! (:export-timer owner) nil)
                               (swap! (:state owner) assoc :preparing false)
                               (let [{:keys [start end]} @(:state owner)]
                                 (invoke-trim! owner "exportRange(qlonglong,qlonglong)"
                                               [(long (Math/round (* 1000 start)))
                                                (long (Math/round (* 1000 end)))]))))))
        nil))))

(defn view [owner]
  (let [{:keys [duration start end playhead playing priming preparing error media media-duration
                busy problem progress thumbnails]} @(:state owner)
        editable (editable? @(:state owner) busy)
        {:keys [shortcuts hints]} @settings/published
        play-hint (get hints "reviewPlayPause" "")
        play-label (if (or priming (= media (value/enum "LoadingMedia"))) "Loading…"
                     (if playing "Pause" "Play"))]
    [:vbox {:id "review-root" :hexpand true :vexpand true :padding 24 :spacing 14}
     [:label {:label "Review recording" :font-size 24 :font-weight 600 :color "#f0f3f7" :hexpand true}]
     [:label {:label "Drag the white handle to seek, or the blue handles to trim."
              :color "#aab3c0" :wrap (value/enum "WordWrap") :hexpand true}]
     [:vbox {:id "review-preview" :hexpand true :vexpand true :color "#07090b" :radius 10 :padding 4}
      [:overlay {:hexpand true :vexpand true}
       [:video-player {:id "review-video" :anchor "fill" :on-first-frame #(finish-priming! owner)}]
       [:center {:visible (boolean (seq error))}
        [:label {:id "review-playback-error" :label error :color "#f0b5b5"}]]
     (for [[action id command args] [["reviewPlayPause" "review-space" :play-pause []]
                                     ["reviewSeekBackward" "review-left" :seek [-1]]
                                     ["reviewSeekForward" "review-right" :seek [1]]
                                     ["reviewSeekBackwardFar" "review-shift-left" :seek [-5]]
                                     ["reviewSeekForwardFar" "review-shift-right" :seek [5]]
                                     ["reviewMarkStart" "review-mark-start" :mark-start []]
                                     ["reviewMarkEnd" "review-mark-end" :mark-end []]
                                     ["reviewCancel" "review-escape" :cancel []]]]
       [:shortcut {:key id :id id :sequences (get shortcuts action [])
                   :enabled (if (= command :cancel) true editable)
                   :on-activate #(apply command! owner command args)}])]]
     [:playback-controls {:id "review-playback-controls" :hexpand true
                          :elapsed (fmt playhead) :total (fmt duration) :label play-label
                          :icon (str "qrc:/icons/" (if (and playing (not priming)) "pause" "play") ".svg")
                          :can-play (and editable (pos? media-duration) (not priming))
                          :tooltip (str play-label (when (seq play-hint) (str " (" play-hint ")")))
                          :on-play #(command! owner :play-pause)}]
     [:trim-bar {:id "review-trim" :hexpand true :preferred-height 108 :sensitive editable
                 :duration duration :start start :end end :playhead playhead
                 :thumb-count 12 :thumbnails thumbnails
                 :start-label (fmt start) :end-label (fmt end)
                 :on-seek #(seek! owner %)
                 :on-start #(handle! owner :start %)
                 :on-end #(handle! owner :end %)
                 :on-interacting-change #(interacting! owner %)}]
     [:hbox {:hexpand true}
      [:label {:label (str "From " (fmt start)) :color "#91bff0"}]
      [:spacer {:hexpand true}]
      [:label {:label (str "To " (fmt end)) :color "#91bff0"}]]
     (when (or (seq problem) busy)
       [:label {:label (if (seq problem) problem
                          (str "Exporting trimmed recording…"
                               (when (>= progress 0) (str " " (long (Math/round (* 100 progress))) "%"))))
                :hexpand true :wrap (value/enum "WordWrap")
                :color (if (seq problem) "#f0b5b5" "#aab3c0")}])
     [:hbox {:hexpand true :spacing 10}
      (when (trimmed? @(:state owner))
        [:action-button {:id "review-keep" :label "Keep original"
                         :sensitive (not (or busy preparing))
                         :on-click #(command! owner :keep)}])
      [:spacer {:hexpand true}]
      (when (or busy preparing)
        [:action-button {:id "review-cancel-export" :label "Cancel export"
                         :on-click #(command! owner :cancel)}])
      [:action-button {:id "review-save" :label (if (trimmed? @(:state owner)) "Save trim" "Keep original")
                       :appearance "primary" :font-size 14 :font-weight 600 :natural-height true
                       :sensitive (if (trimmed? @(:state owner)) editable (not (or busy preparing)))
                       :on-click #(command! owner (if (trimmed? @(:state owner)) :export :keep))}]]
]))

(defn- refresh-trim! [owner]
  (when-not @(:closed owner)
    (let [trim (:trim owner)
          path (trim/property trim "path")
          duration (trim/property trim "duration")
          busy (trim/property trim "busy")
          problem (trim/property trim "problem")
          progress (trim/property trim "progress")
          thumbnails (vec (trim/property trim "thumbnails"))]
      (swap! (:state owner)
             #(cond-> (assoc % :busy busy :problem problem :progress progress
                             :thumbnails thumbnails)
                (pos? duration) (set-duration duration)))
      (cond
        (empty? path) ((:on-finished owner))
        (and (:dismiss-after-cancel @(:state owner)) (not busy))
        (do (swap! (:state owner) assoc :dismiss-after-cancel false)
            (invoke-trim! owner "keepOriginal()" []))
        (and (not busy) (not (:preparing @(:state owner))))
        (when-let [video (surface owner)]
          (when (= (value/url "") (:source (media/state video)))
            (media/source! video (trim/property trim "source"))))))))
(defn open! [root trim on-finished]
  (let [owner {:root root :trim trim :window (atom nil) :closed (atom false)
               :state (r/atom (initial (trim/property trim "duration")))
               :prime-timer (atom nil) :export-timer (atom nil) :clock (atom nil)
               :on-finished on-finished}]
    (try
      (let [window (qt/open-window! #(view owner) :title "Snitt — Review recording"
                                    :width 960 :height 690
                                    :on-close (fn [_] (command! owner :close) false))]
        (q/set-property! window "minimumWidth" 680)
        (q/set-property! window "minimumHeight" 520)
        (reset! (:window owner) window)
        (let [video (surface owner)]
          (media/source! video (trim/property trim "source"))
          (doseq [[property handler]
                  [[:source #(do (when (= % (value/url ""))
                                  (swap! (:state owner) assoc :primed false :priming false
                                         :playing false :error ""))
                                (when (and (not= % (value/url ""))
                                           (:primed @(:state owner)))
                                  (swap! (:state owner) assoc :primed false :priming false)))]
                   [:duration #(do (swap! (:state owner) assoc :media-duration %)
                                   (invoke-trim! owner "setDuration(qlonglong)" [%])
                                   (swap! (:state owner) set-duration %))]
                   [:media #(do (swap! (:state owner) assoc :media %)
                                (when (contains? #{(value/enum "LoadedMedia")
                                                   (value/enum "BufferedMedia")} %)
                                  (prime! owner)))]
                   [:position #(when (and (not (:playing @(:state owner)))
                                          (not (:priming @(:state owner)))
                                          (not (:interacting @(:state owner))))
                                 (swap! (:state owner) seek (/ (double %) 1000.0)))]
                   [:playback #(when-not (:priming @(:state owner))
                                 (swap! (:state owner) assoc :playing
                                        (= % (value/enum "PlayingState"))))]
                   [:error #(when (and (not= % (value/enum "NoError")) (not (busy? owner)))
                              (swap! (:state owner) assoc :playing false :error
                                     (str "Playback unavailable: "
                                          (:error-string (media/state video)))))]]]
            (media/observe! video video property
              #(when-not @(:closed owner) (handler %))))
          (object/subscribe! video (:handle trim) "changed()" #(refresh-trim! owner))
          (reset! (:clock owner)
                  (qt/every! video 40
                    (fn [{:keys [delta-seconds]}]
                      (when-not @(:closed owner)
                        (let [before @(:state owner)
                              after (clock-step before delta-seconds
                                                (:rate (media/state video)))]
                          (when (not= before after)
                            (reset! (:state owner) after)
                            (when (and (:playing before) (not (:playing after)))
                              (pause-at-playhead! owner))))))))
          (refresh-trim! owner))
        owner)
      (catch Throwable error
        (when-let [window @(:window owner)] (qt/close-window! window))
        (throw error)))))
(defn dispose! [owner]
  (when (and owner (not @(:closed owner)))
    (reset! (:closed owner) true)
    (doseq [token [@(:prime-timer owner) @(:export-timer owner) @(:clock owner)]]
      (when token (qt/cancel! token)))
    (when-let [video (surface owner)]
      (object/disconnect-owner! video)
      (media/clear! video))
    (when-let [window @(:window owner)]
      (when (qt/window-alive? window) (qt/close-window! window)))))
