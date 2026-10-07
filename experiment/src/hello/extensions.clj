(ns hello.extensions
  "Application-owned QML resources and native factories."
  (:require [glimmer.core :as ui]
            [glimmer.ratom :as r]
            [glimmer-qt.core :as qt]
            [glimmer-qt.widget :as w]
            [glimmer-qt.ffi :as q]
            [jolt.ffi :as ffi]))

(ffi/defcfn register-native "hello_register_extensions" [] :int)
(defonce installed? (atom false))
(def disposed (atom 0))
(def spinner-source "qrc:/hello/Spinner.qml")
(w/register! :hello/spinner
  {:source spinner-source :relay "relay" :children "content"
   :props {:id ["objectName" :string ""] :angle ["angle" :number 0]}
   :required #{:angle} :events {:on-activate :string}
   :dispose #(swap! disposed inc)})
(w/register! :hello/marker
  {:source "native:hello.marker" :children :self
   :props {:id ["objectName" :string ""] :angle ["angle" :number 0]}
   :required #{:angle} :dispose #(swap! disposed inc)})
(w/register! :hello/service
  {:source "native:hello.service" :props {:id ["objectName" :string ""]}
   :dispose #(swap! disposed inc)})

(defn install-native!
  "Call on Qt's thread before the first render creates a native control."
  []
  (when-not @installed?
    (when (zero? (register-native)) (throw (ex-info (q/native-error) {})))
    (reset! installed? true)))

(def rotation (r/atom 0.0))
(defn demo []
  (install-native!)
  [:hello/spinner {:id "spinner" :angle @rotation}
   [:label {:label "Application-owned spinner"}]])
(defn -main [& _]
  (ui/run demo :title "Custom Qt spinner"
          :on-ready #(qt/every! (w/find-by-id "spinner") 16
                      (fn [{:keys [elapsed-seconds]}]
                        (reset! rotation (mod (* 180.0 elapsed-seconds) 360.0))))))
