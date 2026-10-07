(ns snitt.sessions
  "Jolt owns editor lifetimes, focus and capture admission. Native objects are
  borrowed from exactly one session and never used after that session closes."
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]))

(defn manager [create & {:keys [start-capture]}]
  {:state (atom {:next-id 0 :order [] :sessions {} :last-active nil
                 :capture nil :hidden [] :restore-focus nil})
   :create create :start-capture start-capture})

(defn snapshot [manager] @(:state manager))
(defn session [manager id] (get-in (snapshot manager) [:sessions id]))
(defn sessions [manager] (mapv #(session manager %) (:order (snapshot manager))))
(defn- current? [manager id] (some? (session manager id)))
(defn- live-window? [window] (and window (qt/window-alive? window)))

(defn activate! [manager id]
  (when-let [{:keys [window review-window]} (session manager id)]
    (let [target (if (and (live-window? review-window)
                          (seq (q/read-property (get-in (session manager id) [:services :trim]) "path")))
                   review-window window)]
      (when (live-window? target) (qt/activate! target)))))

(defn active! [manager id]
  (when (current? manager id)
    (swap! (:state manager) assoc :last-active id)))

(declare close! complete-capture!)

(defn- watch-session! [manager id]
  (let [{:keys [window services on-recording-saved on-recording-changed]} (session manager id)
        backend (:backend services)
        trim (:trim services)
        subscribe (fn [owner target signal handler]
                    (object/subscribe! owner target signal
                      (fn [& args] (when (current? manager id) (apply handler args)))))]
    (subscribe window window "activeChanged()"
               #(when (q/read-property window "active") (active! manager id)))
    (subscribe (:root services) backend "captureFinished(bool)"
               (fn [captured]
                 (when-not (or (q/read-property backend "recording")
                               (seq (q/read-property trim "path")))
                   (complete-capture! manager id captured))))
    (subscribe (:root services) backend "recordingSaved(QString)"
               (fn [path]
                 (when on-recording-saved (on-recording-saved manager id path))
                 (complete-capture! manager id true)))
    (subscribe (:root services) backend "recordingCanceled()"
               #(complete-capture! manager id false))
    (subscribe (:root services) backend "recordingChanged()"
               #(when on-recording-changed (on-recording-changed manager id)))
    (subscribe (:root services) backend "error(QString)"
               (fn [message]
                 (let [{:keys [capture restore-focus last-active order]} (snapshot manager)
                       target-id (if (= capture id)
                                   (or restore-focus last-active
                                       (last (remove #{id} order)) id)
                                   id)]
                   (when-let [report (:on-error (session manager target-id))]
                     (report target-id message)))
                 (when (and (not (q/read-property backend "capturing"))
                            (not (q/read-property backend "recording")))
                   (complete-capture! manager id false))))
    (subscribe (:root services) trim "finalized(QString)"
               (fn [_]
                 (qt/later! (:root services) 0
                   #(when-let [{:keys [window]} (session manager id)]
                      (when-not (q/read-property window "visible")
                        (close! manager id))))))))

(defn create! [manager label]
  (let [id (:next-id (swap! (:state manager) update :next-id inc))
        result (assoc ((:create manager) id label #(close! manager id)) :id id)]
    (try
      (swap! (:state manager)
             (fn [s] (-> s (assoc-in [:sessions id] result)
                         (update :order conj id))))
      (watch-session! manager id)
      id
      (catch Throwable e
        (swap! (:state manager)
               (fn [s] (-> s (update :sessions dissoc id)
                           (update :order (fn [ids] (vec (remove #{id} ids)))))))
        (when-let [window (:window result)] (qt/close-window! window))
        (when-let [root (get-in result [:services :root])] (widget/dispose! root))
        (throw e)))))

(defn show-editor! [manager]
  (when-not (:capture (snapshot manager))
    (let [{:keys [last-active order]} (snapshot manager)
          id (or (when (current? manager last-active) last-active)
                 (last order) (create! manager nil))]
      (activate! manager id)
      id)))

(defn open-image! [manager url label]
  (let [id (create! manager label)
        {:keys [load-url]} (session manager id)]
    (when-not (load-url url)
      (close! manager id)
      (throw (ex-info "Could not open image" {:url url})))
    (activate! manager id)
    id))

(defn- restore-hidden! [manager hidden focus]
  (doseq [[id visibility] hidden]
    (when-let [window (:window (session manager id))]
      (when (live-window? window)
        (q/set-property! window "visibility" visibility))))
  (when-let [window (:window (session manager focus))]
    (when (live-window? window)
      (qt/raise! window)
      (qt/request-activate! window))))

(defn complete-capture! [manager id captured]
  (when (= id (:capture (snapshot manager)))
    (let [{:keys [hidden restore-focus]} (snapshot manager)]
      (swap! (:state manager) assoc :capture nil :hidden [] :restore-focus nil)
      (restore-hidden! manager hidden nil)
      (if (and captured (current? manager id))
        (activate! manager id)
        (do (close! manager id)
            (restore-hidden! manager [] restore-focus))))))

(defn capture! [manager multiple video]
  (let [{:keys [capture order]} (snapshot manager)]
    (when-not capture
      (if-let [review (first (filter
                              #(seq (q/read-property (get-in % [:services :trim]) "path"))
                              (sessions manager)))]
        (activate! manager (:id review))
        (let [id (create! manager nil)
              existing (remove #{id} order)
              hidden (vec (keep (fn [other]
                                  (let [window (:window (session manager other))]
                                    (when (and (live-window? window)
                                               (q/read-property window "visible"))
                                      [other (q/read-property window "visibility")])))
                                existing))
              focus (first (filter (fn [other]
                                     (q/read-property (:window (session manager other)) "active"))
                                   existing))]
          (swap! (:state manager) assoc :capture id :hidden hidden :restore-focus focus)
          (doseq [[other _] hidden] (qt/hide! (:window (session manager other))))
          (try
            (if-let [start (:start-capture manager)]
              (start manager id multiple video)
              (object/invoke! (get-in (session manager id) [:services :backend])
                              "capture(bool,bool)" [(boolean multiple) (boolean video)]))
            (catch Throwable e
              (complete-capture! manager id false)
              (throw e)))
          id)))))

(defn hotkey-capture! [manager]
  (if-let [id (:capture (snapshot manager))]
    (when-let [backend (get-in (session manager id) [:services :backend])]
      (when (q/read-property backend "recording")
        (object/invoke! backend "stopRecordingFromHotkey()" [])))
    (capture! manager false false)))

(defn set-review-window! [manager id window]
  (when (current? manager id)
    (swap! (:state manager) assoc-in [:sessions id :review-window] window)))

(defn set-controls-window! [manager id window]
  (when (current? manager id)
    (swap! (:state manager) assoc-in [:sessions id :controls-window] window)))

(defn close! [manager id]
  (when-let [{:keys [window review-window controls-window services]} (session manager id)]
    (let [capturing? (= id (:capture (snapshot manager)))
          {:keys [hidden restore-focus]} (snapshot manager)]
      ;; Remove identity before disposing Qt objects. Queued deliveries, even
      ;; from a backend being destroyed, cannot target this or a later session.
      (swap! (:state manager)
             (fn [s] (-> s (update :sessions dissoc id)
                         (update :order (fn [ids] (vec (remove #{id} ids))))
                         (update :hidden (fn [entries] (vec (remove #(= id (first %)) entries))))
                         (update :last-active #(when (not= id %) %))
                         (update :restore-focus #(when (not= id %) %))
                         (cond-> capturing?
                           (assoc :capture nil :hidden [] :restore-focus nil)))))
      (object/disconnect-owner! window)
      (object/disconnect-owner! (:root services))
      (when (live-window? review-window) (qt/close-window! review-window))
      (when (live-window? controls-window) (qt/close-window! controls-window))
      (when (live-window? window) (qt/close-window! window))
      (when (q/alive? (:root services)) (widget/dispose! (:root services)))
      (when capturing? (restore-hidden! manager hidden restore-focus))
      true)))
