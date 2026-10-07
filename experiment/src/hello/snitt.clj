(ns hello.snitt
  "Snitt-owned constructors and calls over glimmer-qt's typed extension seam."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [jolt.ffi :as ffi]))

(ffi/defcfn fixture-root "snitt_fixture_root" [:string] :int)
(ffi/defcfn register-native "snitt_register_experiment" [] :int)
(defonce registered? (atom false))

(widget/register! :snitt/canvas
  {:source "native:snitt.canvas" :children :none
   :props {:id ["objectName" :string ""]
           :width ["width" :number 320] :height ["height" :number 240]
           :tool ["tool" :string "rect"]}})
(widget/register! :snitt/services
  {:source "native:snitt.services"
   :props {:id ["objectName" :string ""]}})
(widget/register! :snitt/selector
  {:source "native:snitt.selector"
   :props {:id ["objectName" :string ""]}})

(defn install!
  "The caller supplies a disposable directory before its first Qt render.
  The library's factories remain registered for the lifetime of the process."
  [directory]
  (when-not @registered?
    (when (zero? (fixture-root directory))
      (throw (ex-info "Snitt fixture root requires an active QApplication and existing directory." {})))
    (when (zero? (register-native))
      (throw (ex-info (q/native-error) {})))
    (reset! registered? true)))

(defn expose-services! [service]
  (when-not (object/invoke! service "expose()" [])
    (throw (ex-info (q/native-error) {})))
  (into {} (map (fn [key] [key (:value (q/read-property service (name key)))])
                [:settings :backend :trim])))

(defn reload-settings! [service]
  (object/invoke! service "reloadSettings()" []))

(defn open-trim! [service path]
  (object/invoke! service "openTrim(QString)" [path]))

(defn load-image! [canvas path]
  (object/invoke! canvas "load(QUrl)" [(value/local-file path)]))

(defn drag! [canvas x1 y1 x2 y2]
  (object/invoke! canvas "begin(double,double)" [(double x1) (double y1)])
  (object/invoke! canvas "end(double,double)" [(double x2) (double y2)]))

(defn undo! [canvas] (object/invoke! canvas "undo()" []))
