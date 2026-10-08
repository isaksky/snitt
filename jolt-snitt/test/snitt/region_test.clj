(ns snitt.region-test
  (:require [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.app :as app]
            [snitt.region :as region]
            [snitt.sessions :as sessions]
            [snitt.settings :as settings]
            [jolt.ffi :as ffi]))

(ffi/defcfn pointer! "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(ffi/defcfn key! "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn accessible-mode! "gqt_test_accessible_mode" [:uint64 :int] :int :blocking)

(def completed? (atom false))
(def checks (atom 0))
(def fixture-count (atom 1))
(defn check! [condition message]
  (swap! checks inc)
  (when-not condition (throw (ex-info message {}))))

(defn geometry-text [screen]
  (str/join "," (:geometry screen)))

(defn start-fixture! [image manager id multiple video]
  (let [screen (first (qt/screens))
        backend (get-in (sessions/session manager id) [:services :backend])]
    (check! (object/invoke! backend "pickerFixture(QStringList,QStringList,bool,bool)"
                                 [(vec (repeat @fixture-count image))
                                  (vec (repeat @fixture-count (geometry-text screen))) multiple video])
            "Frozen fixture screen admitted")))

(defn input-screen! [picker screen action x y]
  (let [handle (qt/find-by-id (nth @(:windows picker) screen)
                              (str "picker-input-" screen))]
    (check! (some? handle)
            (str "Glimmer input surface exists: " (pr-str @(:windows picker))))
    (check! (= 1 (pointer! handle action (double x) (double y))) "Real pointer delivery")))

(defn input! [picker action x y] (input-screen! picker 0 action x y))

(defn drag! [picker x1 y1 x2 y2]
  (input! picker 0 x1 y1)
  (input! picker 1 x2 y2)
  (input! picker 2 x2 y2))

(defn accessible-modes! [picker]
  (let [window (first @(:windows picker))
        toolbar (qt/find-by-id window "picker-toolbar-0")
        single (q/child-target toolbar "singleButton")
        multiple (q/child-target toolbar "multipleButton")
        video (q/child-target toolbar "videoButton")]
    (check! (= 7 (accessible-mode! single 0)) "Selected mode exposes checked checkbox state")
    (check! (= 3 (accessible-mode! multiple 0)) "Other mode exposes unchecked checkbox state")
    (check! (= 7 (accessible-mode! multiple 1)) "Accessibility activation checks multiple mode")
    (check! (= :multiple (:mode @(:state picker))) "Accessibility activation reaches Jolt mode command")
    (check! (= 7 (accessible-mode! multiple 1)) "Repeated activation keeps selected mode checked")
    (check! (= :multiple (:mode @(:state picker))) "Repeated activation preserves Jolt mode")
    (check! (= 7 (accessible-mode! video 1)) "Accessibility activation checks video mode")
    (check! (= :video (:mode @(:state picker))) "Accessibility activation reaches video command")
    (check! (= 1 (key! window 82 0)) "Keyboard restores single mode after accessible activation")))

(defn exercise! [image]
  (let [screen (first (qt/screens))
        [sx sy sw sh] (:geometry screen)
        source {"width" sw "height" sh "pixelWidth" 800 "pixelHeight" 800}
        negative (assoc source "x" -1400 "y" -200 "width" 700 "height" 500
                        "pixelWidth" 1400 "pixelHeight" 1000)]
    (check! (= [200 100 400 200]
               (region/pixel-rect [100 50 200 100]
                                  {"width" 800 "height" 600 "pixelWidth" 1600 "pixelHeight" 1200}))
            "Pixel mapping matches RegionSelector::pixelRect at 2x")
    (check! (= [0 0 50 50]
               (region/pixel-rect [-10 -10 50 50]
                                  {"width" 800 "height" 600 "pixelWidth" 1000 "pixelHeight" 750}))
            "Clipped fractional scale matches native pixelRect")
    (check! (= [200 100 400 200] (region/pixel-rect [300 150 -200 -100]
                                                       {"width" 800 "height" 600 "pixelWidth" 1600 "pixelHeight" 1200}))
            "Reverse drag normalizes")
    (check! (= [200 100 400 200] (region/pixel-rect [100 50 200 100] negative))
            "Negative global monitor origin does not enter display-local crop")
    (let [video-screen (assoc source "index" 0)
          [pressed _] (region/press (region/initial false true) video-screen
                                    {:button 1 :x 100 :y 150})
          [_ effect] (region/release pressed video-screen {:button 1 :x 220 :y 270})]
      (check! (= :record (:action effect)) "Video drag requests native recording")
      (check! (= [100 150 120 120] (:area effect))
              "Video region remains display-local"))
    (let [manager (:manager @app/runtime)
          id (sessions/capture! manager false false)]
      (qt/later! 250
        (fn []
          (let [picker (:picker (sessions/snapshot manager))
                _ (check! (and picker (= 1 (count @(:windows picker))))
                          (str "One Glimmer overlay shown: "
                               (pr-str (select-keys (sessions/snapshot manager) [:capture :picker]))))
                window (first @(:windows picker))
                image-item (qt/find-by-id window "picker-background-0")]
            (check! (= "ready" (q/read-property image-item "loadState"))
                    "Frozen native pixels render through image provider")
            (drag! picker 100 150 220 270)
            (check! (nil? (:capture (sessions/snapshot manager)))
                    (str "Single drag completed capture: " (pr-str @(:state picker))))
            (check! (zero? (count @(:windows picker))) "Single overlay disposed")
            (let [canvas (app/canvas (:context (sessions/session manager id)))
                  [_ _ expected-width expected-height] (region/pixel-rect [100 150 120 120] source)]
              (check! (= expected-width (q/read-property canvas "imageWidth")) "Single crop width")
              (check! (= expected-height (q/read-property canvas "imageHeight")) "Single crop height"))
            (sessions/close! manager id)
            (let [multi (sessions/capture! manager true false)]
              (qt/later! 250
                (fn []
                  (let [picker (:picker (sessions/snapshot manager))]
                  (drag! picker 100 150 220 270)
                  (drag! picker 280 160 400 280)
                  (check! (= 2 (count (:regions @(:state picker)))) "Ordered regions added")
                  (check! (= [0 1] (mapv :token (:regions @(:state picker)))) "Native crop handles in order")
                  (input! picker 0 300 180)
                  (input! picker 2 300 180)
                  (check! (= 1 (count (:regions @(:state picker)))) "Click removes containing region")
                  (drag! picker 280 160 400 280)
                  (check! (= 2 (count (:regions @(:state picker)))) "Add again after removal")
                  (check! (= 1 (key! (first @(:windows picker)) 16777219 0)) "Backspace reaches shortcut")
                  (check! (= 1 (count (:regions @(:state picker)))) "Remove-last shortcut")
                  (spit (q/read-property (get-in @app/runtime [:host :settings]) "settingsFile")
                        "[Shortcuts]\nregionArrange=A\n")
                  (settings/reload!)
                  (check! (= "A" (get-in @settings/published [:hints "regionArrange"]))
                          "Configured picker shortcut published")
                  (check! (= 1 (count (:regions @(:state picker)))) "Settings hint update keeps selection")
                  (qt/later! 150
                    (fn []
                  (let [button (q/child-target (qt/find-by-id (first @(:windows picker)) "picker-toolbar-0") "arrangeButton")]
                    (check! (str/includes? (q/read-property button "tooltip") "A")
                            (str "Shortcut hint refreshes live: " (q/read-property button "tooltip")))
                    (check! (= 1 (key! (first @(:windows picker)) 65 0))
                            "Updated Arrange key reaches picker"))
                  (check! (nil? (:capture (sessions/snapshot manager))) "Arrange accepted regions")
                  (check! (zero? (count @(:windows picker))) "All picker overlays disposed")
                  (check! (= 1 (q/read-property (app/canvas (:context (sessions/session manager multi)))
                                                "regionCount"))
                          "Accepted region reaches document")
                  (sessions/close! manager multi)
                  (let [cancel-id (sessions/capture! manager false false)]
                    (qt/later! 250
                      (fn []
                        (let [picker (:picker (sessions/snapshot manager))
                              window (first @(:windows picker))]
                          (do (accessible-modes! picker) (check! (= 1 (key! window 86 0)) "V key reaches picker"))
                          (check! (= :video (:mode @(:state picker))) "Video mode selected in Jolt")
                          (check! (= 1 (key! window 86 0)) "Repeated V key reaches picker")
                          (check! (= :video (:mode @(:state picker))) "Active video mode does not toggle off")
                          (check! (= 1 (key! window 77 0)) "M during video does not bypass disabled mode")
                          (check! (= :video (:mode @(:state picker))) "Multiple regions stays unavailable in video mode")
                          (check! (= 1 (key! window 82 0)) "R key reaches picker")
                          (check! (= :single (:mode @(:state picker))) "Single mode restored")
                          (input! picker 0 100 150)
                          (input! picker 3 100 150)
                          (check! (nil? (:drag @(:state picker))) "Pointer cancellation clears draft")
                          (check! (= 1 (key! window 16777216 0)) "Escape reaches picker")
                          (check! (nil? (:capture (sessions/snapshot manager))) "Escape cancels capture")
                          (check! (nil? (sessions/session manager cancel-id)) "Canceled editor removed")
                          (reset! fixture-count 2)
                          (qt/later! 150
                            (fn []
                              (let [multi-id (sessions/capture! manager true false)]
                                (qt/later! 250
                                  (fn []
                                    (let [picker (:picker (sessions/snapshot manager))]
                                      (check! (= 2 (count @(:windows picker))) "One Glimmer overlay per captured monitor")
                                      (doseq [screen [0 1]]
                                        (input-screen! picker screen 0 100 150)
                                        (input-screen! picker screen 1 220 270)
                                        (input-screen! picker screen 2 220 270))
                                      (check! (= [0 1] (mapv :screen (:regions @(:state picker))))
                                              "Regions preserve monitor and capture order")
                                      (check! (= 1 (key! (first @(:windows picker)) 16777216 0))
                                              "Cancel from one monitor")
                                      (check! (nil? (:capture (sessions/snapshot manager)))
                                              "Capture canceled across monitors")
                                      (check! (nil? (sessions/session manager multi-id))
                                              "Multi-monitor capture editor removed")
                                      (qt/later! 100
                                        (fn []
                                          (check! (= 0 (qt/window-count)) "All monitor overlays disposed")
                                          (reset! completed? true)
                                          (qt/quit!)))))))))))))))))))))))))

(defn -main [& _]
  (try
    (qt/run-application!
     #(do (app/start! :background true :capture-start (partial start-fixture! (System/getenv "SNITT_JOLT_IMAGE")))
          (qt/later! 100 #(exercise! (System/getenv "SNITT_JOLT_IMAGE"))))
     :app-id "snitt-jolt-development" :keep-alive true :auto-quit-ms 12000)
    (check! @completed? "Region picker sequence completed")
    (check! (zero? (widget/live-count)) "Managed widgets released")
    (check! (zero? (q/owned-count)) "Native objects released")
    (println "PASS:" @checks "Glimmer region picker checks")
    (finally (reset! app/runtime nil) (shutdown-agents))))
