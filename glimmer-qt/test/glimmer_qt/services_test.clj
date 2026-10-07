(ns glimmer-qt.services-test
  (:require [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.services :as services]
            [glimmer-qt.widget :as widget]
            [jolt.ffi :as ffi]))

(ffi/defcfn service-hooks "gqt_test_service_hooks" [:uint64 :int] :int)
(ffi/defcfn service-url "gqt_test_service_url" [] :string)
(ffi/defcfn service-file "gqt_test_service_file" [] :string)
(ffi/defcfn service-fixture "gqt_test_service_fixture" [:string] :int)
(ffi/defcfn unsupported "gqt_test_service_unsupported" [:uint64] :int)

(def checks (atom 0))
(defn check! [ok message]
  (swap! checks inc)
  (when-not ok (throw (ex-info message {}))))
(defn success [result message]
  (check! (:ok? result) (str message ": " (:error result)))
  (:value result))
(defn failure [result message]
  (check! (false? (:ok? result)) message))

(defn run-checks! []
  (let [session @widget/session
        dir (System/getenv "SNITT_SERVICES_FIXTURE_DIR")
        source (str dir "/source # ü.png")
        output (str dir "/saved & ü.png")
        url "https://example.test/a%23%20%C3%BC?q=x%26y"]
    (check! (= 1 (q/gui-thread)) "Services run on the GUI thread")
    (check! (= 1 (unsupported session)) "Private clipboard fixture installed")
    (failure (services/read-text) "Unsupported clipboard text reports failure")
    (failure (services/read-image) "Unsupported clipboard image reports failure")
    (failure (services/publish-text! "a\u0000b") "Embedded NUL cannot truncate text")
    (check! (= 1 (service-fixture source)) "Known RGBA image fixture saved")
    (let [original (success (services/load-image source) "Load fixture")
          unicode "Café 漢字 🦉 # &"]
      (success (services/publish-text! unicode) "Publish Unicode")
      (check! (= unicode (success (services/read-text) "Read Unicode")) "Unicode round trip")
      (success (services/publish-image! original) "Publish image")
      (let [copied (success (services/read-image) "Read image")]
        (success (services/publish-text! "clipboard replaced") "Replace clipboard owner")
        (check! (true? (success (services/same-image? original copied) "Compare images"))
                "QImage pixels and format survive clipboard replacement")
        (success (services/save-image! copied output) "Save copied image")
        (let [reloaded (success (services/load-image output) "Reload copied image")]
          (check! (true? (success (services/same-image? copied reloaded) "Compare saved image"))
                  "PNG fixture round trip is lossless")
          (success (services/release-image! reloaded) "Release reloaded image"))
        (check! (= 1 (service-hooks session 1)) "Install test hooks")
        (failure (services/publish-image! original) "Publication failure is reported")
        (failure (services/publish-text! "could not publish") "Text publication failure is reported")
        (check! (true? (success (services/same-image? original copied) "Images remain owned"))
                "Failure leaves owned document available")
        (success (services/open-url! url) "Open URL hook")
        (check! (= url (service-url)) "URL encoding preserves Unicode and punctuation")
        (success (services/reveal-file! output) "Reveal saved path hook")
        (check! (= output (service-file)) "Reveal hook receives unmodified UTF-8 path")
        (check! (= 1 (service-hooks session 2)) "Install failing hooks")
        (failure (services/open-url! url) "URL failure reported")
        (failure (services/open-url! "relative path") "Relative URL rejected")
        (failure (services/reveal-file! output) "Reveal failure reported")
        (failure (services/reveal-file! (str dir "/missing.png")) "Missing path rejected")
        (success (services/release-image! copied) "Release clipboard image")
        (failure (services/save-image! copied output) "Released image cannot be used"))
      (success (services/release-image! original) "Release source image"))
    (check! (= 1 (service-hooks session 0)) "Remove test hooks")))

(defn -main [& _]
  (try
    (let [done (atom false)]
      (qt/run-application! (fn [])
                           :on-ready #(try (run-checks!) (reset! done true)
                                           (finally (qt/quit!)))
                           :auto-quit-ms 5000)
      (check! @done "Service checks completed"))
    (println "PASS:" @checks "isolated Qt service checks")
    (finally (shutdown-agents))))
