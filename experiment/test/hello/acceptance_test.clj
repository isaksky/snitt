(ns hello.acceptance-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.media :as media]
            [glimmer-qt.object :as object]
            [glimmer-qt.tray :as tray]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [hello.acceptance :as app]
            [hello.snitt :as snitt]
            [jolt.ffi :as ffi]
            [jolt.host :as host]))

(ffi/defcfn write-images "hello_write_test_images" [:string] :int)
(ffi/defcfn click "gqt_test_click" [:uint64] :int :blocking)
(ffi/defcfn key "gqt_test_key" [:uint64 :int :int] :int :blocking)
(ffi/defcfn paste-native "gqt_test_paste" [:uint64 :pointer] :int :blocking)
(ffi/defcfn pointer "gqt_test_pointer" [:uint64 :int :double :double] :int :blocking)
(ffi/defcfn slider "gqt_test_slider_input" [:uint64 :int :double] :int :blocking)
(ffi/defcfn file-result-native "gqt_test_file_result" [:uint64 :pointer] :int :blocking)

(def directory (System/getenv "SNITT_ACCEPTANCE_DIR"))
(def clip (System/getenv "SNITT_ACCEPTANCE_CLIP"))
(def screenshot (System/getenv "SNITT_ACCEPTANCE_SCREENSHOT"))
(def checks (atom 0))
(def complete? (atom false))
(def canceled-callback? (atom false))
(declare verify-review! verify-background! verify-worker!)

(defn check! [truth message]
  (swap! checks inc)
  (when-not truth
    (throw (ex-info message {:events @app/events :review @app/review}))))
(defn id [name] (or (widget/find-by-id name)
                     (throw (ex-info "Missing acceptance control" {:id name}))))
(defn prop [name property] (q/read-property (id name) property))
(defn paste! [window text]
  (ffi/with-c-string [s text] (paste-native window s)))
(defn file-result! [handle path]
  (ffi/with-c-string [s path] (file-result-native handle s)))
(defn wait-for! [predicate timeout-ms then]
  (let [started (System/currentTimeMillis)]
    (letfn [(poll []
              (cond (predicate) (then)
                    (> (- (System/currentTimeMillis) started) timeout-ms)
                    (throw (ex-info "Acceptance condition timed out"
                                    {:review @app/review :events @app/events
                                     :media (media/state (id "review-video"))}))
                    :else (qt/later! 40 poll)))]
      (poll))))

(defn verify-editors! [image]
  (let [{:keys [left right service selector]} @app/handles
        left-canvas (qt/find-by-id left "left-canvas")
        right-canvas (qt/find-by-id right "right-canvas")]
    (check! (= 3 (qt/window-count)) "Two editors and review own independent windows")
    (check! (and left-canvas right-canvas (not= left-canvas right-canvas))
            "Each editor mounts a distinct native canvas")
    (check! (and (q/alive? service) (q/alive? selector)) "Native services share Qt runtime")
    (check! (prop "acceptance-selector" "sharedApplication") "QWidget selector shares QApplication")
    (check! (snitt/load-image! left-canvas image) "Editor A loads isolated image")
    (check! (snitt/load-image! right-canvas image) "Editor B loads isolated image")
    (check! (= 240 (q/read-property left-canvas "imageWidth")) "Native image dimensions")
    (check! (= 240 (q/read-property right-canvas "imageWidth")) "Second native image")
    (qt/activate! left)
    (check! (= 1 (click (id "left-cut"))) "Real toolbar click")
    (check! (= "cut" (:tool @(app/editor-state :left))) "Toolbar changes editor A")
    (check! (= "rect" (:tool @(app/editor-state :right))) "Editor B remains independent")
    (check! (= 1 (pointer (id "left-pointer") 0 60.0 60.0)) "Real canvas pointer press")
    (check! (= 1 (pointer (id "left-pointer") 2 60.0 60.0)) "Real canvas pointer release")
    (check! (some #(= [:left :pointer 60.0] %) @app/events) "Pointer event reached policy")
    (snitt/drag! left-canvas 40 40 140 40)
    (check! (< (q/read-property left-canvas "imageWidth") 240) "Native cut edits image")
    (check! (= 240 (q/read-property right-canvas "imageWidth")) "Native edit isolation")
    (widget/focus! (id "left-annotation"))
    (check! (= 1 (paste! left "First line\nSecond 🦊 line")) "Real multiline paste")
    (check! (= "" (:annotation @(app/editor-state :right))) "Annotation isolation")
    (widget/focus! (id "left-rect"))
    (check! (= 1 (key left 32 0)) "Real keyboard activation")
    (when screenshot
      (check! (= 1 (q/save-window @widget/session screenshot)) "Visual evidence saved"))
    (q/set-property! left "width" 280)))

(defn verify-dialogs! [image]
  (check! (= 1 (click (id "left-arrange"))) "Open confirmation by pointer")
  (qt/later! 130
    (fn []
      (check! (widget/dialog-visible? (id "left-confirm")) "Confirmation is visible")
      (check! (= 1 (click (id "left-reject"))) "Reject by pointer")
      (qt/later! 130
        (fn []
          (check! (some #{[:left :cancel]} @app/events) "Cancel callback once")
          (widget/open-dialog! (id "left-confirm"))
          (qt/later! 130
            (fn []
              (check! (= 1 (click (id "left-accept"))) "Accept by pointer")
              (check! (some #{[:left :confirm]} @app/events) "Confirm callback")
              (check! (= "" (:annotation @(app/editor-state :left))) "Confirm clears annotation")
              (check! (= 1 (click (id "left-open"))) "Open file chooser by pointer")
              (qt/later! 150
                (fn []
                  (check! (widget/dialog-visible? (id "left-file")) "File chooser is nonblocking")
                  (check! (= 1 (file-result! (id "left-file") image)) "Real file chooser result")
                  (qt/later! 130
                    (fn []
                      (check! (some #(and (= :left (first %)) (= :file (second %))) @app/events)
                              "File selection reached editor")
                      (verify-review!))))))))))))

(defn verify-review! []
  (let [surface (id "review-video")]
    (check! (= 4 (count (filter #(widget/find-by-id (str "thumb-" %)) (range 4))))
            "Thumbnail filmstrip renders four isolated images")
    (check! (= 1 (slider (id "trim-start") 0 0.0)) "Trim start press")
    (check! (= 1 (slider (id "trim-start") 1 0.25)) "Trim start drag")
    (check! (= 1 (slider (id "trim-start") 2 0.25)) "Trim start release")
    (check! (< 0 (:start @app/review) 1.5) "Trim start responds to pointer")
    (check! (= 1 (slider (id "trim-end") 0 1.0)) "Trim end press")
    (check! (= 1 (slider (id "trim-end") 1 0.75)) "Trim end drag")
    (check! (= 1 (slider (id "trim-end") 2 0.75)) "Trim end release")
    (check! (< 1.5 (:end @app/review) 3) "Trim end responds to pointer")
    (media/mute! surface true)
    (media/source! surface (value/local-file clip))
    (wait-for! #(pos? (:duration (media/state surface))) 7000
      (fn []
        (check! (pos? (:duration (media/state surface))) "Real local media loaded")
        (check! (= 1 (click (id "play"))) "Playback button receives pointer input")
        (wait-for! #(some #{[:review :first-frame]} @app/events) 7000
          (fn []
            (check! (= (value/enum "PlayingState") (:playback (media/state surface)))
                    "Real media playback started")
            (check! (= 1 (slider (id "seek") 0 0.0)) "Seek press")
            (check! (= 1 (slider (id "seek") 1 0.5)) "Seek drag")
            (check! (= 1 (slider (id "seek") 2 0.5)) "Seek release")
            (check! (< 0.5 (:position @app/review) 2.5) "Scrub updates review state")
            (check! (= 1 (click (id "play"))) "Pause by pointer")
            (check! (= (value/enum "PausedState") (:playback (media/state surface)))
                    "Playback paused")
            (verify-background!)))))))

(defn verify-background! []
  (let [{:keys [left right tray tray-open selector]} @app/handles
        stale (qt/find-by-id left "left-canvas")
        previous (count @app/events)]
    (check! (q/alive? tray) "Tray owns background lifetime")
    (when (= "cocoa" (System/getenv "QT_QPA_PLATFORM"))
      (check! (tray/available?) "macOS desktop exposes native tray")
      (check! (q/read-property tray "visible") "Native tray icon is visible"))
    (qt/hide! left)
    (qt/hide! right)
    (q/invoke! tray-open "trigger()" [])
    (check! (= [:tray :open] (last @app/events)) "Tray restores editor")
    (check! (q/read-property left "visible") "Editor restored from tray")
    (let [screen (first (qt/screens))
          [x y] (:available screen)
          overlay (qt/open-window! app/overlay-root :title "Capture selection"
                                   :screen screen :x x :y y :width 220 :height 130
                                   :tool true :frameless true :topmost true
                                   :input-transparent true :transparent-background true)]
      (check! (q/read-property overlay "visible") "Capture overlay is visible")
      (check! (some #{"WindowTransparentForInput"} (:value (q/read-property overlay "flags")))
              "Capture overlay passes input through")
      (qt/close-window! overlay))
    (check! (q/alive? selector) "QWidget capture selector remains owned")
    (qt/later! left 400 #(reset! canceled-callback? true))
    (qt/reload-window! left)
    (qt/later! 130
      (fn []
        (check! (not (q/alive? stale)) "Reload invalidates old native canvas")
        (check! (q/alive? (qt/find-by-id left "left-canvas")) "Reload remounts canvas")
        (check! (q/alive? (qt/find-by-id right "right-canvas")) "Other editor survives reload")
        (qt/close-window! left)
        (qt/later! 130
          (fn []
            (check! (not (q/alive? stale)) "Closed root cannot revive old canvas")
            (check! (q/alive? tray) "Tray lives after editor closes")
            (check! (q/alive? right) "Second editor remains open")
            (check! (not @canceled-callback?) "Window-owned work canceled")
            (check! (zero? (count @(:watches (app/editor-state :left))))
                    "Closed editor detaches reactive watcher")
            @(future (swap! (app/editor-state :left) assoc :status "late worker update"))
            (check! (nil? (widget/find-by-id "left-status"))
                    "Late worker update cannot remount closed editor")
            (check! (>= (count @app/events) previous) "Delivered callbacks remain bounded")
            (let [again (qt/open-window! (app/editor-root :left
                                           (str directory "/a # ü.png"))
                                         :title "Snitt editor reopened"
                                         :width 360 :height 300)
                  first-canvas (qt/find-by-id again "left-canvas")]
              (qt/reload-window! again)
              (qt/later! 130
                (fn []
                  (check! (not (q/alive? first-canvas)) "Repeat reload invalidates prior canvas")
                  (check! (q/alive? (qt/find-by-id again "left-canvas"))
                          "Reopened editor remounts")
                  (qt/activate! again)
                  (check! (= 1 (click (qt/find-by-id again "left-cut")))
                          "Reopened editor receives fresh pointer callback")
                  (qt/close-window! again)
                  (qt/later! 130
                    (fn []
                      (check! (not (qt/window-alive? again)) "Repeat close releases editor")
                      (check! (not (q/alive? first-canvas)) "No stale canvas after repeat close")
                      (check! (zero? (count @(:watches (app/editor-state :left))))
                              "Repeat close detaches watcher")
                      (verify-worker!))))))))))))

(defn verify-worker! []
  (let [right (:right @app/handles)]
    (swap! app/review assoc :busy? true :progress 0.0 :error "")
    (future
      (qt/later! 0 #(swap! app/review assoc :busy? false :progress 0.6
                                  :error "Fixture encoder error")))
    (wait-for! #(= 0.6 (:progress @app/review)) 3000
      (fn []
        (check! (= "Fixture encoder error" (prop "review-error" "text"))
                "Worker update reaches review error UI")
        (check! (= 0.6 (prop "export-progress" "position"))
                "Worker progress reaches native control")
        (check! (= 1 (click (id "cancel-export"))) "Cancel by pointer")
        (check! (:canceled? @app/review) "Cancel state recorded")
        (check! (zero? (:progress @app/review)) "Cancel clears progress")
        (qt/close-window! right)
        (qt/later! 200
          (fn []
            (check! (not @canceled-callback?) "No stale callback after close")
            (check! (= 1 (qt/window-count)) "Review alone keeps background runtime")
            (qt/close-window! (:review @app/handles))
            (qt/later! 130
              (fn []
                (check! (zero? (qt/window-count)) "Tray keeps runtime alive without windows")
                (check! (q/alive? (:tray @app/handles)) "Tray survives last window")
                (q/invoke! (:tray-open @app/handles) "trigger()" [])
                (qt/later! 130
                  (fn []
                    (check! (= 1 (qt/window-count)) "Tray reopens an editor")
                    (check! (q/alive? (qt/find-by-id (:left @app/handles) "left-canvas"))
                            "Reopened editor has a fresh native canvas")
                    (reset! complete? true)
                    (q/invoke! (:tray-quit @app/handles) "trigger()" [])))))))))))

(defn -main [& _]
  (try
    (check! (and (string? directory) (string? clip)) "Isolated fixture paths supplied")
    (check! (= 1 (write-images directory)) "Generated isolated image fixtures")
    (let [image (str directory "/a # ü.png")]
      (qt/run-application!
       #(do (app/start! directory image)
            (qt/later! 400
              (fn []
                (verify-editors! image)
                (qt/later! 130
                  (fn []
                    (check! (= "First line\nSecond 🦊 line" (:annotation @(app/editor-state :left)))
                            (str "Controlled multiline annotation: "
                                 (:annotation @(app/editor-state :left))))
                    (check! (= "rect" (:tool @(app/editor-state :left)))
                            "Keyboard command reached editor")
                    (check! (> (prop "left-arrange" "y") (prop "left-rect" "y"))
                            "Toolbar wraps when the editor narrows")
                    (q/set-property! (:left @app/handles) "width" 600)
                    (verify-dialogs! image))))))
       :auto-quit-ms 24000)
      (check! @complete? "Acceptance sequence completed before deadline")
      (check! (zero? (widget/live-count)) "All managed controls released")
      (check! (zero? (object/live-count)) "All native observers disconnected")
      (check! (every? #(zero? (count @(:watches (app/editor-state %)))) [:left :right])
              "Editor reactive watchers released after quit")
      (host/call-on-main-thread
       #(check! (zero? (q/owned-count)) "All native objects released"))
      (println "PASS:" @checks "Snitt binding acceptance checks"))
    (finally (shutdown-agents))))
