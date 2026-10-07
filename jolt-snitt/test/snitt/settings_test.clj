(ns snitt.settings-test
  (:require [clojure.java.io :as io]
            [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.native :as native]
            [snitt.settings :as settings]))

(def checks (atom 0))
(def done? (atom false))
(def service-a (atom nil))
(def service-b (atom nil))
(def window (atom nil))
(def path (atom nil))
(def initial (atom nil))
(def notices (atom []))

(defn check! [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {:published @settings/published :notices @notices}))))
(defn color [service] (:value (q/read-property (:settings service) "goodColor")))
(defn error [service] (q/read-property (:settings service) "lastError"))
(defn content [] (slurp @path))
(defn write! [source] (spit @path source))
(defn replace! [source]
  (let [temporary (str @path ".new")]
    (spit temporary source)
    (check! (.renameTo (io/file temporary) (io/file @path)) "Atomic replacement succeeded")))
(defn later! [delay f] (qt/later! delay f))

(defn finish! []
  (widget/dispose! (:root @service-b))
  (widget/dispose! (:root @service-a))
  (qt/close-window! @window)
  (reset! done? true)
  (qt/quit!))

(defn exercise! []
  (reset! service-a (settings/register! (native/create-services!)))
  (reset! path (q/read-property (:settings @service-a) "settingsFile"))
  (reset! initial (content))
  (check! (some? @settings/published) "Initial INI validated in Jolt")
  (check! (= "#22c55e" (:good-color @settings/published)) "Default color")
  (check! (= "Ctrl+Print" (first (get-in @settings/published [:configured "globalCapture"])))
          "Default global shortcut")
  (check! (str/includes? @initial "; Preserve unrelated keys and comments")
          "Default file retained its guide comment")
  (reset! service-b (settings/register! (native/create-services!)))
  (check! (= (color @service-a) (color @service-b)) "Second editor inherits published settings")
  (let [changed (-> @initial
                    (str/replace "good=#22c55e" "good=#317ab5")
                    (str/replace "toolPixelate=P" "toolPixelate=F6|F7")
                    (str/replace "copyClose=C" "copyClose=Ctrl+Shift+C")
                    (str/replace #"(?m)^picturesRoot=.*$"
                                 (str "picturesRoot=\"" (System/getenv "SNITT_JOLT_DATA_DIR")
                                      "/Pictures, shared\""))
                    (str "\n[Unknown]\nvalue=preserved\n; another comment\n"))]
    (write! changed)
    (check! (settings/reload!) "Valid candidate publishes")
    (check! (= "#317ab5" (:good-color @settings/published)) "Color reaches Jolt")
    (check! (= "#ff317ab5" (color @service-a)) "Color reaches first native projection")
    (check! (= (color @service-a) (color @service-b)) "Color reaches all editor projections")
    (check! (= ["F6" "F7"] (get-in @settings/published [:shortcuts "toolPixelate"]))
            "Shortcut alternatives reach Jolt")
    (check! (= ["Meta+Shift+C" "Ctrl+C"] (get-in @settings/published [:shortcuts "copyClose"]))
            "macOS configured Control converts to Qt Meta; Copy alias remains")
    (check! (= "F6 / F7" (get-in @settings/published [:hints "toolPixelate"]))
            "Shortcut hint uses both alternatives")
    (check! (str/includes? (:pictures-root @settings/published) "Pictures, shared")
            "Quoted comma path is normalized")
    (check! (= changed (content)) "Unknown section and comments remain byte-for-byte")
    (write! (str/replace changed "toolPixelate=F6|F7" "toolPixelate="))
    (check! (settings/reload!) "Optional shortcut can be disabled")
    (check! (empty? (get-in @settings/published [:shortcuts "toolPixelate"]))
            "Disabled shortcut has no binding")
    (check! (empty? (get-in @settings/published [:hints "toolPixelate"]))
            "Disabled shortcut has no hint")
    (write! "[Colors]\ngood=#317ab5\n")
    (check! (settings/reload!) "Missing keys restore defaults")
    (check! (= ["P"] (get-in @settings/published [:shortcuts "toolPixelate"]))
            "Removed shortcut restores default")
    (check! (= (object/invoke! (:settings @service-a) "cleanRoot(QString,QString)"
                               ["" (q/read-property (:settings @service-a) "defaultPicturesRoot")])
               (:pictures-root @settings/published))
            "Removed save root restores platform default")
    (replace! changed)
    (check! (settings/reload!) "Full candidate restores alternatives")
    (let [collision (str/replace changed "toolPixelate=F6|F7" "toolPixelate=R")]
      (replace! collision)
      (check! (not (settings/reload!)) "Conflicting shortcut rejected")
      (check! (str/includes? (error @service-a) "toolRectangle") "Conflict names both keys")
      (check! (= "#ff317ab5" (color @service-b)) "Invalid candidate retains all editors"))
    (let [bad (str/replace changed "good=#317ab5" "good=broken")]
      (replace! bad)
      (check! (not (settings/reload!)) "Invalid color rejected")
      (check! (str/includes? (error @service-b) "Colors/good") "Color diagnostic names key")
      (check! (= "#317ab5" (:good-color @settings/published)) "Published snapshot stays valid"))
    (doseq [[source diagnostic] [[(str changed "\n[broken") "INI syntax"]
                                 [(str/replace changed "good=#317ab5" "good=#317ab5,#ffffff")
                                  "Colors/good"]
                                 [(str/replace changed "toolPixelate=F6|F7" "toolPixelate=F6|")
                                  "Shortcuts/toolPixelate"]
                                 [(str/replace changed "toolPixelate=F6|F7" "toolPixelate=F6|F6")
                                  "Shortcuts/toolPixelate"]
                                 [(str/replace changed #"(?m)^picturesRoot=.*$" "picturesRoot=relative")
                                  "Save/picturesRoot"]]]
      (replace! source)
      (check! (not (settings/reload!)) (str "Rejected candidate: " diagnostic))
      (check! (str/includes? (error @service-a) diagnostic)
              (str "Diagnostic identifies " diagnostic))
      (check! (= "#317ab5" (:good-color @settings/published))
              "Malformed candidate keeps published snapshot"))
    (let [native-failure (str/replace changed "globalCapture=Ctrl+Print" "globalCapture=Meta+F21")]
      (replace! native-failure)
      (check! (not (settings/reload!)) "Unsupported native global registration rolls back")
      (check! (str/includes? (error @service-a) "Shortcuts/globalCapture")
              "Native registration diagnostic names key")
      (check! (= "Ctrl+Print" (first (get-in @settings/published [:configured "globalCapture"])))
              "Previous global shortcut remains published"))
    (replace! changed)
    (check! (settings/reload!) "Corrected candidate recovers")
    (check! (empty? (error @service-a)) "Recovery clears diagnostic")
    (io/delete-file @path)
    (check! (not (settings/reload!)) "Deletion retains previous settings")
    (check! (= "#317ab5" (:good-color @settings/published)) "Deletion retains color")
    (write! changed)
    (check! (settings/reload!) "Recreation recovers")
    (let [ordinary (str/replace changed "good=#317ab5" "good=#4688b2")]
      (write! ordinary)
      (later! 700
              (fn []
                (check! (= "#4688b2" (:good-color @settings/published))
                        "Ordinary save reloads through watcher")
                (let [replacement (str/replace ordinary "good=#4688b2" "good=#638cab")]
                  (replace! replacement)
                  (later! 700
                          (fn []
                            (check! (= "#638cab" (:good-color @settings/published))
                                    "Atomic replacement reloads through watcher")
                            (io/delete-file @path)
                            (later! 700
                                    (fn []
                                      (check! (str/includes? (error @service-a) "missing")
                                              "Watcher reports deletion")
                                      (check! (= "#638cab" (:good-color @settings/published))
                                              "Watcher retains last valid candidate")
                                      (write! replacement)
                                      (later! 700
                                              (fn []
                                                (check! (empty? (error @service-a))
                                                        "Watcher recovers after recreation")
                                                (finish!)))))))))))))

(defn -main [& _]
  (try
    (settings/configure! :on-error #(swap! notices conj %))
    (qt/run-application!
     #(do
        (native/install!)
        (reset! window (qt/open-window! (fn [] [:label {:label "settings"}])
                                       :title "Settings test" :width 120 :height 80))
        (later! 100 exercise!))
     :app-id "snitt-jolt-development" :keep-alive true :auto-quit-ms 7000)
    (check! @done? "Settings test completed")
    (check! (zero? (widget/live-count)) "All service handles released")
    (println "PASS:" @checks "Jolt settings checks")
    (finally (settings/clear!) (shutdown-agents))))
