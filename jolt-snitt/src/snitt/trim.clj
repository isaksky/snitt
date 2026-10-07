(ns snitt.trim
  "Jolt owns trim/filmstrip state. Native jobs only execute processes and files."
  (:require [clojure.string :as str]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]))

(defn initial []
  {:path "" :duration 0 :busy false :phase :idle :progress 0.0 :problem ""
   :thumbnails [] :jobs {} :serial 0 :temporary "" :directory ""
   :window-start 0 :window-end 0 :index 0 :lookback 0 :eof false :buffer ""})
(defn property [owner property]
  (if (= property "source") (value/local-file (:path @(:state owner)))
      (get @(:state owner) (keyword property))))
(defn- native! [owner signature args] (@(:invoke owner) (:handle owner) signature args))
(defn- notify! [owner] (native! owner "notify()" []))
(defn- size [owner path] (native! owner "size(QString)" [path]))
(defn- timestamp [ms] (format "%.6f" (/ (double ms) 1000.0)))
(defn- probe-args [path] ["-v" "error" "-print_format" "json" "-show_format"
                         "-show_streams" "-select_streams" "v:0" path])
(defn valid-clip? [info] (every? pos? (map #(get info % 0) ["duration" "width" "height"])))
(defn valid-duration? [duration selected] (<= (- selected 100) duration (+ selected 2)))
(defn progress-lines [buffer chunk duration]
  (let [parts (str/split (str buffer chunk) #"\n" -1)
        values (keep #(when (str/starts-with? (str/trim %) "out_time_us=")
                        (try (max 0.0 (min 1.0 (/ (Double/parseDouble (subs (str/trim %) 12))
                                                   (* 1000.0 (max 1 duration)))))
                             (catch Throwable _ nil))) (butlast parts))]
    [(last parts) (last values)]))
(defn sample-time [{:keys [window-start window-end duration index]}]
  (+ window-start (long (Math/round (/ (* (double (- (if (pos? window-end) window-end duration)
                                                    window-start)) (+ index 0.5)) 12.0)))))
(defn- stop! [owner channel]
  ;; Invalidate queued events before waiting for process termination.
  (swap! (:state owner) update :jobs dissoc channel)
  (native! owner "stop(QString)" [channel]))
(defn- stop-previews! [owner]
  (let [source (stop! owner "source") thumb (stop! owner "thumbnail")]
    (and source thumb)))
(defn- start! [owner channel tool args]
  (let [generation (:serial (swap! (:state owner) update :serial inc))]
    (swap! (:state owner) assoc-in [:jobs channel] generation)
    (if (native! owner "start(QString,int,QString,QStringList)" [channel generation tool args]) true
        (do (swap! (:state owner) update :jobs dissoc channel) false))))
(defn- tool [owner name] (native! owner "tool(QString)" [name]))
(defn- cleanup! [owner]
  (let [path (:temporary @(:state owner))]
    (when (seq path) (native! owner "remove(QString)" [path]))
    (swap! (:state owner) assoc :temporary "")))
(declare next-thumbnail! thumbnail-attempt! event! set-duration!)
(defn- fail! [owner message]
  (cleanup! owner)
  (swap! (:state owner) assoc :phase :review :busy false :progress 0.0 :problem message :index 0)
  (notify! owner)
  (next-thumbnail! owner))
(defn- clear! [owner]
  (when (stop-previews! owner)
    (cleanup! owner)
    (native! owner "clearThumbnails()" [])
    (let [serial (:serial @(:state owner))]
      (reset! (:state owner) (assoc (initial) :serial serial)))
    (notify! owner)
    true))
(defn keep! [owner]
  (when (and (not (property owner "busy")) (seq (property owner "path")))
    (let [path (property owner "path")]
      (if (clear! owner) (native! owner "finalize(QString)" [path])
          (do (swap! (:state owner) assoc :problem "Could not stop the preview worker.") (notify! owner))))))
(defn open! [owner path]
  (when (and (empty? (property owner "path")) (not (property owner "busy")) (>= (size owner path) 0))
    (swap! (:state owner) assoc :path path :phase :review :thumbnails (vec (repeat 12 ""))
           :directory (native! owner "thumbnailDirectory()" []))
    (notify! owner)
    (let [probe (tool owner "ffprobe")]
      (when (seq probe) (start! owner "source" probe (probe-args path))))))
(defn set-duration! [owner ms]
  (when (and (seq (property owner "path")) (pos? ms) (zero? (property owner "duration")))
    (swap! (:state owner) assoc :duration ms)
    (notify! owner)
    (next-thumbnail! owner)))
(defn window! [owner start end]
  (let [s @(:state owner) duration (:duration s)
        start (max 0 (min (dec duration) start)) end (max (inc start) (min duration end))]
    (when (and (seq (:path s)) (pos? duration) (not (:busy s))
               (not= [start end] [(:window-start s) (if (pos? (:window-end s)) (:window-end s) duration)]))
      (if (stop-previews! owner)
        (do (swap! (:state owner) assoc :window-start start :window-end end :problem "" :index 0
                   :thumbnails (vec (repeat 12 "")) :directory (native! owner "thumbnailDirectory()" []))
            (notify! owner) (next-thumbnail! owner))
        (do (swap! (:state owner) assoc :problem "Could not refresh the filmstrip preview.") (notify! owner))))))
(defn- thumbnail-path [s] (str (:directory s) "/" (:index s) ".jpg"))
(defn- thumbnail-attempt! [owner]
  (let [s @(:state owner) time (sample-time s) lookback (:lookback s)
        start (max 0 (- time lookback))
        args (into ["-hide_banner" "-loglevel" "error" "-y" "-ss" (timestamp (if (pos? lookback) start time))]
                   (concat (when (pos? lookback) ["-t" (timestamp (max 1 (- time start)))])
                           ["-i" (:path s) "-frames:v" "1" "-vf"
                            (if (pos? lookback) "reverse,scale=-1:90" "scale=-1:90")
                            "-vcodec" "mjpeg" (thumbnail-path s)]))]
    (when-not (start! owner "thumbnail" (tool owner "ffmpeg") args)
      (swap! (:state owner) assoc :problem "Could not start the thumbnail decoder.") (notify! owner))))
(defn- next-thumbnail! [owner]
  (let [s @(:state owner)
        index (first (keep-indexed #(when (and (>= %1 (:index s)) (empty? %2)) %1) (:thumbnails s)))]
    (when (and index (not (:busy s)) (seq (:path s)) (seq (:directory s))
               (pos? (:duration s)) (not (get-in s [:jobs "thumbnail"])))
      (if (empty? (tool owner "ffmpeg"))
        (do (swap! (:state owner) assoc :problem "The thumbnail decoder is unavailable.") (notify! owner))
        (do (swap! (:state owner) assoc :index index :lookback 0 :eof false)
            (thumbnail-attempt! owner))))))
(defn cancel! [owner]
  (when (property owner "busy")
    (let [export (stop! owner "export") probe (stop! owner "probe")]
      (if (and export probe)
        (fail! owner "")
        (do (swap! (:state owner) assoc :phase :stopping :problem "Could not stop the export worker. Cancel again to retry.")
            (notify! owner))))))
(defn export! [owner start end]
  (let [s @(:state owner) duration (:duration s)]
    (when (and (seq (:path s)) (not (:busy s)))
      (cond
        (or (<= duration 0) (< start 0) (<= end start) (> end (+ duration 50)) (< (- end start) 100))
        (do (swap! (:state owner) assoc :problem "Choose a trim range of at least 0.1 seconds.") (notify! owner))
        (and (zero? start) (>= end (dec duration))) (keep! owner)
        (not (stop-previews! owner))
        (do (swap! (:state owner) assoc :problem "Could not stop the preview worker. Your original recording is unchanged.") (notify! owner))
        :else
        (let [temporary (native! owner "temporary(QString)" [(:path s)]) ffmpeg (tool owner "ffmpeg")]
          (swap! (:state owner) assoc :start start :end end :temporary temporary :busy true
                 :phase :exporting :progress 0.0 :buffer "" :problem "")
          (notify! owner)
          (when (or (empty? ffmpeg)
                    (not (start! owner "export" ffmpeg
                      ["-nostdin" "-hide_banner" "-loglevel" "error" "-y" "-progress" "pipe:1"
                       "-ss" (timestamp start) "-i" (:path s) "-t" (timestamp (- end start))
                       "-map" "0:v:0" "-map" "0:a?" "-c" "copy" "-use_editlist" "1"
                       "-movflags" "+faststart" temporary])))
            (fail! owner "FFmpeg is unavailable for trimming. Your original recording is unchanged.")))))))
(defn- thumbnail-finished! [owner success error]
  (let [s @(:state owner) file (thumbnail-path s) decoded (and success (pos? (size owner file)))
        eof (or (:eof s) (and (zero? (:lookback s)) (<= (size owner file) 0)
                             (str/includes? error "before EOF")
                             (str/includes? error "Nothing was written into output file")))
        time (sample-time s)]
    (cond
      decoded
      (let [url (native! owner "localUrl(QString)" [file])]
        (swap! (:state owner) update :thumbnails
               #(reduce (fn [v i] (assoc v i url)) % (if eof (range (:index s) 12) [(:index s)])))
        (swap! (:state owner) update :index inc)
        (notify! owner) (next-thumbnail! owner))
      (and eof (or (zero? (:lookback s)) (< (:lookback s) time)))
      (do (swap! (:state owner) assoc :eof true :lookback
                 (if (zero? (:lookback s)) (min 2000 (max 1 time)) (min time (* 2 (:lookback s)))))
          (native! owner "remove(QString)" [file]) (thumbnail-attempt! owner))
      :else
      ;; Leave this slot empty for resume after export/cancel, but advance this
      ;; generation without spinning forever on a permanently undecodable slot.
      (do (swap! (:state owner) assoc :problem "Some filmstrip frames could not be decoded.")
          (notify! owner)
          (when (< (:index s) 11)
            (swap! (:state owner) assoc :index (inc (:index s)) :lookback 0 :eof false)
            (thumbnail-attempt! owner))))))
(defn- export-finished! [owner success error]
  (let [s @(:state owner)]
    (cond
      (or (not success) (< (size owner (:temporary s)) 1024))
      (fail! owner (str "Could not export the selected range" (when (seq error) (str ": " (str/trim error)))
                        ". Your original recording is unchanged."))
      (not (native! owner "limit(QString,qlonglong)" [(:temporary s) (- (:end s) (:start s))]))
      (fail! owner "Could not set the trimmed MP4 playback range. Your original recording is unchanged.")
      :else
      (let [probe (tool owner "ffprobe")]
        (swap! (:state owner) assoc :phase :validating)
        (notify! owner)
        (when (or (empty? probe) (not (start! owner "probe" probe (probe-args (:temporary s)))))
          (fail! owner "Could not verify the trimmed MP4. Your original recording is unchanged."))))))
(defn- probe-finished! [owner success output]
  (let [s @(:state owner) info (native! owner "probe(QString)" [output])]
    (cond
      (not (and success (valid-clip? info)))
      (fail! owner "The trimmed MP4 could not be verified. Your original recording is unchanged.")
      (not (valid-duration? (get info "duration") (- (:end s) (:start s))))
      (fail! owner "The trimmed MP4 duration did not match the selected range. Your original recording is unchanged.")
      (not (stop-previews! owner))
      (fail! owner "Could not stop the preview worker. Your original recording is unchanged.")
      :else
      (let [_ (swap! (:state owner) assoc :phase :replacing)
            problem (native! owner "replace(QString,QString)" [(:temporary s) (:path s)])]
        (if (seq problem) (fail! owner (str "Could not replace the original recording. " problem))
            (do (clear! owner) (native! owner "finalize(QString)" [(:path s)])))))))
(defn event! [owner channel generation success output error]
  (when (= generation (get-in @(:state owner) [:jobs channel]))
    (swap! (:state owner) update :jobs dissoc channel)
    (case channel
      "source" (when success
                 (let [info (native! owner "probe(QString)" [output])]
                   (when (valid-clip? info) (set-duration! owner (get info "duration")))))
      "thumbnail" (thumbnail-finished! owner success error)
      "export" (export-finished! owner success error)
      "probe" (probe-finished! owner success output)
      nil)))
(defn invoke! [owner signature args]
  (case signature
    "setDuration(qlonglong)" (apply set-duration! owner args)
    "setThumbnailWindow(qlonglong,qlonglong)" (apply window! owner args)
    "exportRange(qlonglong,qlonglong)" (apply export! owner args)
    "cancelExport()" (cancel! owner)
    "keepOriginal()" (if (property owner "busy") (cancel! owner) (keep! owner))))
(defn create! [handle]
  (let [owner {:handle handle :state (atom (initial)) :invoke (atom object/invoke!)}]
    (object/subscribe! handle handle "finished(QString,int,bool,QString,QString)"
                       #(event! owner %1 %2 %3 %4 %5))
    (object/subscribe! handle handle "output(QString,int,QString)"
      (fn [channel generation chunk]
        (when (and (= channel "export") (= generation (get-in @(:state owner) [:jobs channel])))
          (let [s @(:state owner) [buffer progress] (progress-lines (:buffer s) chunk (- (:end s) (:start s)))]
            (swap! (:state owner) #(cond-> (assoc % :buffer buffer) (some? progress) (assoc :progress progress)))
            (when (some? progress) (notify! owner))))))
    owner))
