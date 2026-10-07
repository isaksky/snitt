(ns snitt.native
  "Application-owned Snitt objects exposed through glimmer-qt's factory seam."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.value :as value]
            [glimmer-qt.widget :as widget]
            [jolt.ffi :as ffi]))

(ffi/defcfn register-native "snitt_jolt_register" [] :int)
(defonce ^:private registered? (atom false))

(widget/register! :snitt/canvas
  {:source "native:snitt-jolt.canvas" :children :none
   :props {:id ["objectName" :string ""]
           :width ["width" :number 800] :height ["height" :number 500]
           :tool ["tool" :string "rect"]}})
(widget/register! :snitt/services
  {:source "native:snitt-jolt.services" :children :none
   :props {:id ["objectName" :string ""]}})

(defn install! []
  (when-not @registered?
    (when (zero? (register-native))
      (throw (ex-info (q/native-error) {})))
    (reset! registered? true)))

(defn create-services!
  "Return an owned root and borrowed descendants. Dispose the root with
  widget/dispose! before closing the application session."
  []
  (let [root (widget/create! :snitt/services {:id "snitt-services"})]
    (try
      (when-not (object/invoke! root "expose()" [])
        (throw (ex-info (q/native-error) {})))
      {:root root
       :settings (:value (q/read-property root "settings"))
       :backend (:value (q/read-property root "backend"))
       :trim (:value (q/read-property root "trim"))}
      (catch Throwable error
        (widget/dispose! root)
        (throw error)))))

(defn load-url! [canvas url]
  (object/invoke! canvas "load(QUrl)" [url]))

(defn load-image! [canvas path]
  (load-url! canvas (value/local-file path)))

(defn begin! [canvas x y]
  (object/invoke! canvas "begin(double,double)" [(double x) (double y)]))
(defn move! [canvas x y]
  (object/invoke! canvas "move(double,double)" [(double x) (double y)]))
(defn end! [canvas x y]
  (object/invoke! canvas "end(double,double)" [(double x) (double y)]))
(defn cancel! [canvas] (object/invoke! canvas "cancel()" []))
(defn undo! [canvas] (object/invoke! canvas "undo()" []))
(defn redo! [canvas] (object/invoke! canvas "redo()" []))
(defn copy! [canvas] (object/invoke! canvas "copy()" []))
(defn save! [canvas] (object/invoke! canvas "save()" []))
