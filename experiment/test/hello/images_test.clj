(ns hello.images-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.value :as v]
            [glimmer.ratom :as r]
            [hello.extensions :as ext]
            [jolt.ffi :as ffi]))

(ffi/defcfn register-provider "hello_register_image_provider" [] :int)
(ffi/defcfn write-images "hello_write_test_images" [:string] :int)
(ffi/defcfn provider-requests "hello_provider_requests" [] :int)
(ffi/defcfn provider-releases "hello_provider_releases" [] :int)
(ffi/defcfn buffer-releases "hello_buffer_releases" [] :int)
(ffi/defcfn sample-pixel "hello_sample_image_pixel" [:string :int :int] :int)
(def checks (atom 0))
(def done? (atom false))
(def events (atom []))
(def window (atom nil))
(def fixtures (System/getenv "SNITT_IMAGE_FIXTURE_DIR"))
(def png (v/local-file (str fixtures "/a # ü.png")))
(def jpg (v/local-file (str fixtures "/b & ü.jpg")))
(def retina (v/local-file (str fixtures "/retina@2x.png")))
(def bad (v/local-file (str fixtures "/invalid.png")))
(def missing (v/local-file (str fixtures "/missing # ü.png")))
(def rows (r/atom [{:id "icon" :source (v/url "qrc:/hello/icons/check.svg") :fill "fit"}
                   {:id "png" :source png :fill "fit"}
                   {:id "jpg" :source jpg :fill "crop"}
                   {:id "retina" :source retina :fill "fit"}
                   {:id "native" :source (v/url "image://hello/thumbnail") :fill "fit"}]))
(defn check! [truth message]
  (swap! checks inc)
  (when-not truth (throw (ex-info message {}))))
(defn throws! [f]
  (check! (try (f) false (catch Throwable _ true)) "Invalid image prop rejected"))
(defn widget [id] (or (w/find-by-id id) (throw (ex-info "Missing image" {:id id}))))
(defn property [id name] (q/read-property (widget id) name))
(defn ready! [id]
  (check! (= "ready" (property id "loadState")) (str id " loaded"))
  (check! (property id "hasPixels") (str id " has displayed pixels")))
(defn root []
  (ext/install-native!)
  [:vbox {:hexpand true :vexpand true}
    (for [{:keys [id source fill]} @rows]
     [:image (cond-> {:key id :id id :source source
                      :preferred-width 80 :preferred-height 80 :cache false
                      :fill-mode fill
                      :on-status #(swap! events conj [id :status %])
                      :on-loaded #(swap! events conj [id :loaded])
                      :on-error #(swap! events conj [id :error %])}
               (not= id "retina") (assoc :source-width 64 :source-height 64))])])
(defn steps! [steps]
  (when-let [step (first steps)]
    (qt/later! 180 #(do (step) (steps! (rest steps))))))
(defn begin! []
  (let [icon (widget "icon") native (widget "native")]
    (doseq [id ["icon" "png" "jpg" "retina" "native"]] (ready! id))
    (check! (< (abs (- (property "retina" "implicitWidth") 24)) 0.01)
            "@2x source uses logical width")
    (check! (= (v/url "qrc:/hello/icons/check.svg")
               (property "icon" "imageSource")) "Application qrc SVG URL")
    (check! (= 64 (property "png" "decodeWidth")) "Bounded decode width")
    (check! (< (abs (- (/ (property "png" "paintedWidth")
                          (property "png" "paintedHeight")) 2.0)) 0.05)
            "PNG aspect fit")
    (check! (< (abs (- (/ (property "jpg" "paintedWidth")
                          (property "jpg" "paintedHeight")) 2.0)) 0.05)
            "JPEG aspect crop")
    (check! (pos? (provider-requests)) "Native provider requested")
    (check! (some #(= ["icon" :loaded] %) @events) "Synchronous SVG completion delivered")
    (let [path (str fixtures "/loaded.png")
          [x y] (q/map-point (widget "png") (qt/window-container @window) 40 40)]
      (q/save-window @w/session path)
      (check! (= 0xc92550 (sample-pixel path (int x) (int y))) "PNG rendered in Qt window"))
    (let [overlay (w/create! :overlay {})
          positioned (w/create! :image {:source png :x 2 :y 3 :width 20 :height 10})]
      (w/append-child! :overlay overlay positioned)
      (check! (not (q/read-property positioned "positionManaged")) "Image uses overlay position")
      (check! (not (q/read-property positioned "sizeManaged")) "Image uses overlay size")
      (w/dispose! overlay))
    (throws! #(w/create! :image {:source "raw path"}))
    (throws! #(w/create! :image {:source png :source-width 0}))
    (throws! #(w/create! :image {:source png :fill-mode "other"}))
    (swap! rows (fn [xs] (mapv #(if (= "png" (:id %)) (assoc % :source missing) %) xs)))
    (steps!
     [(fn []
        (check! (= "error" (property "png" "loadState")) "Missing image state")
        (check! (not (property "png" "hasPixels")) "Old image hidden on source failure")
        (let [path (str fixtures "/missing.png")
              [x y] (q/map-point (widget "png") (qt/window-container @window) 40 40)]
          (q/save-window @w/session path)
          (check! (not= 0xc92550 (sample-pixel path (int x) (int y)))
                  "Missing image clears rendered thumbnail"))
        (check! (some #(and (= "png" (first %)) (= :error (second %))
                           (re-find #"missing" (nth % 2))) @events) "Missing-image error callback")
        (swap! rows (fn [xs] (mapv #(if (= "png" (:id %)) (assoc % :source bad) %) xs))))
      (fn []
        (check! (= "error" (property "png" "loadState")) "Invalid image state")
        (check! (not (property "png" "hasPixels")) "Invalid image has no pixels")
        (swap! rows (fn [xs] (mapv #(if (= "png" (:id %)) (assoc % :source jpg :fill "crop") %) xs))))
      (fn []
        (ready! "png")
        (check! (< (abs (- (/ (property "png" "paintedWidth")
                              (property "png" "paintedHeight")) 2.0)) 0.05)
                "Source replacement repaints")
        (reset! rows [{:id "png" :source png :fill "fit"}]))
      (fn []
        (check! (not (q/alive? icon)) "Removed SVG image disposed")
        (check! (not (q/alive? native)) "Removed native image disposed")
        (check! (nil? (w/find-by-id "native")) "Removed thumbnail has no stale handle")
        (ready! "png")
        (reset! rows []))
      (fn []
        (check! (nil? (w/find-by-id "png")) "Entire changing list unmounted")
        (reset! done? true)
        (qt/quit!))])))
(defn -main [& _]
  (try
    (check! (string? fixtures) "Temporary image directory provided")
    (qt/run-application!
     #(do
        (ext/install-native!)
        (check! (= 1 (write-images fixtures)) "PNG/JPEG fixtures written")
        (check! (= 1 (register-provider)) "Image provider registered")
        (check! (try (zero? (register-provider)) (catch Throwable _ true))
                "Duplicate provider rejected")
        (reset! window (qt/open-window! root :width 120 :height 400))
        (qt/later! 250 begin!))
     :auto-quit-ms 5000)
    (check! @done? "Image test finished")
    (check! (= 1 (provider-releases)) "Provider state released after session")
    (check! (and (pos? (provider-requests))
                 (= (provider-requests) (buffer-releases)))
            "Every provider-owned pixel buffer released")
    (check! (zero? (w/live-count)) "All image handles released")
    (println "PASS:" @checks "image checks")
    (finally (shutdown-agents))))
