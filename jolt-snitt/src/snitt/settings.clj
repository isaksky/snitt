(ns snitt.settings
  "The Jolt settings contract and one published snapshot for all editors.
  Qt supplies paths, key-sequence spelling and file notifications only."
  (:require [clojure.string :as str]
            [clojure.set :as set]
            [glimmer.ratom :as r]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]))

(def definitions
  [["globalCapture" "Ctrl+Print" "Ctrl+Print" :global]
   ["capture" "Ctrl+N" "Meta+N" :base]
   ["captureMultiple" "M" "M" :base]
   ["captureVideo" "V" "V" :base]
   ["openImage" "Ctrl+O" "Meta+O" :base]
   ["pasteImage" "Ctrl+V" "Meta+V" :base]
   ["copyClose" "C" "C" :base]
   ["saveClose" "S" "S" :base]
   ["dismiss" "Escape" "Escape" :base]
   ["undo" "Ctrl+Z" "Meta+Z" :annotate]
   ["redo" "Ctrl+Y" "Meta+Shift+Z" :annotate]
   ["toolCut" "X" "X" :annotate]
   ["toolRectangle" "R" "R" :annotate]
   ["toolHighlight" "H" "H" :annotate]
   ["toolText" "T" "T" :annotate]
   ["toolArrow" "A" "A" :annotate]
   ["toolPixelate" "P" "P" :annotate]
   ["toolErase" "E" "E" :annotate]
   ["goodMode" "G" "G" :annotate]
   ["badMode" "B" "B" :annotate]
   ["arrange" "Return|Enter" "Return|Enter" :arrange]
   ["columnsMore" "Shift+=|+|=" "Shift+=|+|=" :arrange]
   ["columnsLess" "-" "-" :arrange]
   ["removeRegion" "Backspace|Delete" "Backspace|Delete" :arrange]
   ["moveRegionLeft" "Left" "Left" :arrange]
   ["moveRegionRight" "Right" "Right" :arrange]
   ["regionCancel" "Escape" "Escape" :region]
   ["regionSingle" "R" "R" :region]
   ["regionMultiple" "M" "M" :region]
   ["regionVideo" "V" "V" :region]
   ["regionArrange" "Return|Enter" "Return|Enter" :region]
   ["regionRemoveLast" "Backspace|Delete" "Backspace|Delete" :region]
   ["reviewPlayPause" "Space" "Space" :review]
   ["reviewSeekBackward" "Left" "Left" :review]
   ["reviewSeekForward" "Right" "Right" :review]
   ["reviewSeekBackwardFar" "Shift+Left" "Shift+Left" :review]
   ["reviewSeekForwardFar" "Shift+Right" "Shift+Right" :review]
   ["reviewMarkStart" "Ctrl+Space" "Ctrl+Space" :review]
   ["reviewMarkEnd" "Alt+Space" "Alt+Space" :review]
   ["reviewCancel" "Escape" "Escape" :review]])

(defonce published (r/atom nil))
(defonce controller (atom nil))

(defn- invalid! [message] (throw (ex-info message {})))

(defn- scalar [raw key]
  (let [raw (str/trim raw)]
    (if (str/starts-with? raw "\"")
      (do
        (when-not (and (>= (count raw) 2) (str/ends-with? raw "\""))
          (invalid! "Could not parse or read the settings file. Correct the INI syntax; Snitt is keeping the last valid settings."))
        (let [body (subs raw 1 (dec (count raw)))]
          (str/replace body #"\\([\\\"nrt])"
                       (fn [[_ escaped]] ({"n" "\n" "r" "\r" "t" "\t"} escaped escaped)))))
      (do
        (when (and key (or (str/includes? raw ",") (str/includes? raw ";")))
          (invalid! (str key " must be one text value. Quote values containing commas or semicolons with double quotes; use | between shortcut alternatives.")))
        raw))))

(defn parse-ini [source]
  (reduce
   (fn [{:keys [section values] :as result} line]
     (let [line (str/trim line)]
       (cond
         (or (empty? line) (str/starts-with? line ";") (str/starts-with? line "#")) result
         (re-matches #"\[([^\]]+)\]" line)
         (assoc result :section (second (re-matches #"\[([^\]]+)\]" line)))
         (re-matches #"([^=]+)=(.*)" line)
         (let [[_ name raw] (re-matches #"([^=]+)=(.*)" line)
               key (str section "/" (str/trim name))]
           (when (empty? (str/trim name)) (invalid! "Could not parse or read the settings file. Correct the INI syntax."))
           (assoc result :values (assoc values key (scalar raw (when (or (str/starts-with? key "Shortcuts/")
                                                                    (#{"Colors/good" "Colors/bad" "Save/picturesRoot" "Save/videosRoot"} key)) key)))))
         :else (invalid! "Could not parse or read the settings file. Correct the INI syntax; Snitt is keeping the last valid settings."))))
   {:section "General" :values {}}
   (str/split-lines source)))

(defn- contexts [context]
  (if (= context :base) #{:annotate :arrange} #{context}))

(defn- shortcut-info [settings value action]
  (let [info (object/invoke! settings "shortcutInfo(QString)" [value])]
    (when (empty? info)
      (invalid! (str "Shortcuts/" action " has unsupported key syntax: '" value
                     "'. Use one key with optional modifiers.")))
    info))

(defn- build-shortcuts [settings mac? ini]
  (let [bindings
        (vec
         (mapcat
          (fn [[action other mac context]]
            (let [raw (get ini (str "Shortcuts/" action) (if mac? mac other))
                  alternatives (if (str/blank? raw) [] (mapv str/trim (str/split raw #"\|" -1)))]
              (when (and (= action "globalCapture") (empty? alternatives))
                (invalid! "Shortcuts/globalCapture cannot be empty because it controls global capture and recording stop."))
              (when (and (= action "globalCapture") (not= 1 (count alternatives)))
                (invalid! "Shortcuts/globalCapture needs exactly one native system-wide key binding."))
              (let [infos (mapv #(shortcut-info settings % action) alternatives)
                    names (mapv #(get % "normalized") infos)]
                (when (not= (count names) (count (distinct names)))
                  (invalid! (str "Shortcuts/" action " repeats the same key binding '"
                                 (first alternatives) "'.")))
                (mapv (fn [value info] {:action action :context context :value value :info info})
                      alternatives infos))))
          definitions))]
    (doseq [i (range (count bindings)) j (range (inc i) (count bindings))]
      (let [left (nth bindings i) right (nth bindings j)]
        (when (and (not= (:action left) (:action right))
                   (= (get-in left [:info "normalized"]) (get-in right [:info "normalized"]))
                   (or (= :global (:context left)) (= :global (:context right))
                       (seq (set/intersection (contexts (:context left)) (contexts (:context right))))))
          (invalid! (str "Shortcuts/" (:action left) " conflicts with Shortcuts/"
                         (:action right) " at " (:value left) " in an overlapping action context.")))))
    (let [shortcuts (into {} (for [[action] definitions]
                               [action (mapv #(get-in % [:info "portable"])
                                             (filter #(= action (:action %)) bindings))]))
          hints (into {} (for [[action] definitions]
                           [action (str/join " / " (map #(get-in % [:info "hint"])
                                                          (filter #(= action (:action %)) bindings)))]))
          configured (into {} (for [[action] definitions]
                                [action (mapv :value (filter #(= action (:action %)) bindings))]))
          aliased (reduce
                   (fn [current action]
                     (let [alias (object/invoke! settings "standardShortcut(QString)" [action])
                           occupied? (some (fn [{:keys [action context]}]
                                             (and (or (= context :global) (#{:base :annotate :arrange} context))
                                                  (some #{alias} (get current action)))) bindings)]
                       (if (or (empty? (get current action)) occupied?) current
                           (update current action conj alias))))
                   shortcuts ["copyClose" "saveClose"])]
      {:configured configured :shortcuts aliased :hints hints})))

(defn candidate [settings mac? source]
  (let [ini (:values (parse-ini source))
        defaults {:pictures (q/read-property settings "defaultPicturesRoot")
                  :videos (q/read-property settings "defaultVideosRoot")}
        good (str/trim (get ini "Colors/good" "#22c55e"))
        bad (str/trim (get ini "Colors/bad" "#ef4444"))]
    (when-not (re-matches #"#[0-9A-Fa-f]{6}" good)
      (invalid! "Colors/good must use #RRGGBB syntax (for example #22c55e)."))
    (when-not (re-matches #"#[0-9A-Fa-f]{6}" bad)
      (invalid! "Colors/bad must use #RRGGBB syntax (for example #ef4444)."))
    (let [roots (into {} (for [[field key fallback] [[:pictures "picturesRoot" (:pictures defaults)]
                                                    [:videos "videosRoot" (:videos defaults)]]]
                           (let [root (object/invoke! settings "cleanRoot(QString,QString)"
                                                      [(get ini (str "Save/" key) "") fallback])]
                             (when (empty? root)
                               (invalid! (str "Save/" key " must be empty (platform default) or an absolute folder path.")))
                             [field root])))]
      (merge (build-shortcuts settings mac? ini)
             {:good-color (str/lower-case good) :bad-color (str/lower-case bad)
              :pictures-root (:pictures roots) :videos-root (:videos roots)}))))

(defn- native-values [candidate]
  {"shortcuts" (:shortcuts candidate) "hints" (:hints candidate)
   "goodColor" (:good-color candidate) "badColor" (:bad-color candidate)
   "picturesRoot" (:pictures-root candidate) "videosRoot" (:videos-root candidate)})

(defn- live-services []
  (let [live (vec (filter #(q/alive? (:root %)) (:services @controller)))]
    (swap! controller assoc :services live)
    live))

(defn reload! []
  (when-let [{:keys [on-publish on-error]} @controller]
    (when-let [{:keys [root settings]} (first (live-services))]
      (let [path (q/read-property settings "settingsFile")
            outcome (try
                      (let [source (slurp path)
                            next (candidate settings (q/read-property root "macOS") source)
                            wanted (first (get-in next [:configured "globalCapture"]))]
                        (when-not (object/invoke! root "activateGlobalShortcut(QString)" [wanted])
                          (invalid! (str "Could not activate Shortcuts/globalCapture (" wanted "): "
                                         (object/invoke! root "hotkeyError()" [])
                                         ". The previous capture shortcut and all other settings remain active.")))
                        (doseq [{:keys [settings]} (live-services)]
                          (when-not (object/invoke! settings "applyExternal(QVariantMap)" [(native-values next)])
                            (invalid! "Could not publish the validated settings snapshot.")))
                        {:candidate next})
                      (catch Throwable error
                        {:error (if (str/includes? (str error) "No such file")
                                  (str "The settings file is missing. Restore it at " path
                                       "; Snitt is keeping the last valid settings.")
                                  (or (ex-message error) (str error)))}))]
        (if-let [message (:error outcome)]
          (do
            (doseq [{:keys [settings]} (live-services)]
              (object/invoke! settings "externalError(QString)" [message]))
            (when on-error (on-error message))
            false)
          (let [next (:candidate outcome)]
            (reset! published next)
            (when on-publish (on-publish next))
            true))))))

(defn register! [services]
  (when-not @controller (reset! controller {:services []}))
  (swap! controller update :services conj services)
  (object/subscribe! (:root services) (:settings services) "candidateChanged()"
                     #(when (= (:root services) (:root (first (live-services))))
                        (reload!)))
  (object/subscribe! (:root services) (:root services) "globalCaptureActivated()"
                     #(when (= (:root services) (:root (first (live-services))))
                        (when-let [capture (:on-capture @controller)] (capture))))
  (if-let [current @published]
    (do
      (when-not (object/invoke! (:root services) "activateGlobalShortcut(QString)"
                                [(first (get-in current [:configured "globalCapture"]))])
        (invalid! (str "Could not reactivate Shortcuts/globalCapture: "
                       (object/invoke! (:root services) "hotkeyError()" []))))
      (object/invoke! (:settings services) "applyExternal(QVariantMap)" [(native-values current)]))
    (reload!))
  services)

(defn configure! [& {:keys [on-publish on-error on-capture]}]
  (reset! controller {:services [] :on-publish on-publish :on-error on-error
                      :on-capture on-capture})
  (reset! published nil))

(defn clear! []
  (reset! controller nil)
  (reset! published nil))
