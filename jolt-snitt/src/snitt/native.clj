(ns snitt.native
  "Application-owned Snitt objects exposed through glimmer-qt's factory seam."
  (:require [snitt.trim :as trim]
            [snitt.document :as document]
            [snitt.export :as export]
            [glimmer-qt.ffi :as q]
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
(widget/register! :snitt/lifecycle
  {:source "native:snitt-jolt.lifecycle" :children :none
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
       :trim (trim/create! (:value (q/read-property root "trim")))}
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
(defn set-geometry! [canvas {:keys [x y scale]}]
  (object/invoke! canvas "setExternalGeometry(double,double,double)"
                  [(double x) (double y) (double scale)]))
(defn set-tool-sizes! [canvas {:keys [stroke-width text-size pixel-block-size]}]
  (object/invoke! canvas "setExternalToolSizes(int,int,int)"
                  [stroke-width text-size pixel-block-size]))
(defn set-preview! [canvas {:keys [gesture vertical?]}]
  (let [{:keys [tool start end]} gesture]
    (object/invoke! canvas "setExternalPreview(QString,double,double,double,double,bool)"
                    [tool (double (first start)) (double (second start))
                     (double (first end)) (double (second end)) vertical?])))
(defn clear-preview! [canvas]
  (object/invoke! canvas "clearExternalPreview()" []))
(defn region-at [canvas point]
  (object/invoke! canvas "externalRegionAt(double,double)"
                  [(double (first point)) (double (second point))]))
(defn set-drop-region! [canvas index]
  (object/invoke! canvas "setExternalDropRegion(int)" [index]))
(defn draw! [canvas {:keys [kind vertical? from to start end block-size tool stroke-width] :as command}]
  (if (document/owned? canvas)
    (document/edit! canvas command)
    (case kind
      :cut (object/invoke! canvas "drawExternalCut(bool,int,int)" [vertical? from to])
      :blur (object/invoke! canvas "drawExternalBlur(double,double,double,double,int)"
                            [(double (first start)) (double (second start))
                             (double (first end)) (double (second end)) block-size])
      :erase (object/invoke! canvas "drawExternalErase(double,double,double,double)"
                             [(double (first start)) (double (second start))
                              (double (first end)) (double (second end))])
      :annotation (object/invoke! canvas "drawExternalAnnotation(QString,double,double,double,double,int)"
                                  [tool (double (first start)) (double (second start))
                                   (double (first end)) (double (second end)) stroke-width]))))
(defn undo! [canvas]
  (if (document/owned? canvas) (document/undo! canvas) (object/invoke! canvas "undo()" [])))
(defn redo! [canvas]
  (if (document/owned? canvas) (document/redo! canvas) (object/invoke! canvas "redo()" [])))
(defn copy! [canvas] (:ok? (export/copy! canvas)))
(defn save-result! [canvas] (export/save! canvas (q/read-property canvas "saveRoot")))
(defn save! [canvas] (:path (save-result! canvas)))
