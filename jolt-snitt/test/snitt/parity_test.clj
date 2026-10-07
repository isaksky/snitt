(ns snitt.parity-test
  "Run the shared editor command fixture through Jolt's real native canvas."
  (:require [clojure.string :as str]
            [glimmer-qt.core :as qt]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
            [glimmer-qt.widget :as widget]
            [snitt.native :as native]))

(defn- property [canvas name] (q/read-property canvas name))
(defn- flag [x] (if x "true" "false"))
(defn- outcome [x] (if x "true" "false"))

(defn- apply-command! [canvas root [id operation & args]]
  (let [number #(Double/parseDouble %)
        [result saved]
        (case operation
          "new" ["ok" ""]
          "load" [(outcome (native/load-image! canvas (str root "/" (first args)))) ""]
          "bad-load" [(outcome (native/load-image! canvas (str root "/missing.png"))) ""]
          "tool" (do (q/set-property! canvas "tool" (first args)) ["ok" ""])
          "drag" (let [[x y to-x to-y] (mapv number args)]
                   (native/begin! canvas x y)
                   (native/move! canvas to-x to-y)
                   (native/end! canvas to-x to-y)
                   ["ok" ""])
          "cancel-drag" (let [[x y to-x to-y] (mapv number args)]
                          (native/begin! canvas x y)
                          (native/move! canvas to-x to-y)
                          (let [canceled (native/cancel! canvas)]
                            (native/end! canvas to-x to-y)
                            [(outcome canceled) ""]))
          "text" (let [[x y width height text] args]
                   (object/invoke! canvas "addText(double,double,double,double,QString,int)"
                                   [(number x) (number y) (number width) (number height) text 24])
                   ["ok" ""])
          "undo" (do (native/undo! canvas) ["ok" ""])
          "redo" (do (native/redo! canvas) ["ok" ""])
          "clear" (do (object/invoke! canvas "clear()" []) ["ok" ""])
          "copy" [(outcome (native/copy! canvas)) ""]
          "paste" [(outcome (object/invoke! canvas "paste()" [])) ""]
          "save" (let [saved (native/save! canvas)]
                   [(flag (and (string? saved) (not (empty? saved)))) (or saved "")])
          (throw (ex-info "Unknown parity operation" {:operation operation})))]
    [id operation result (flag (property canvas "hasImage"))
     (str (property canvas "imageWidth")) (str (property canvas "imageHeight"))
     (flag (property canvas "canUndo")) (flag (property canvas "canRedo"))
     (property canvas "tool") (str (property canvas "selectedRegion"))
     (flag (property canvas "arranging")) saved]))

(defn- exercise! [commands trace root]
  (native/install!)
  (let [canvases (atom {})]
    (try
      (let [rows (for [[step line] (map-indexed vector (str/split-lines (slurp commands)))
                       :let [[id operation :as command] (str/split line #"\t")
                             canvas (if (= operation "new")
                                      (let [new-canvas (widget/create! :snitt/canvas
                                                        {:width 432 :height 332})]
                                        (q/set-property! new-canvas "saveRoot" (str root "/saved-" id))
                                        (swap! canvases assoc id new-canvas)
                                        new-canvas)
                                      (or (get @canvases id)
                                          (throw (ex-info "Missing parity canvas" {:id id}))))]]
                   (str/join "\t" (let [[case-id op & fields] (apply-command! canvas root command)]
                                      (concat [case-id (str step) op] fields))))]
        (spit trace (str (str/join "\n" (doall rows)) "\n")))
      (finally
        (doseq [canvas (vals @canvases)] (widget/dispose! canvas))
        (qt/quit!)))))

(defn -main [& _]
  (let [commands (System/getenv "SNITT_PARITY_COMMANDS")
        trace (System/getenv "SNITT_PARITY_TRACE")
        root (System/getenv "SNITT_PARITY_ROOT")]
    (when-not (every? #(and % (not (empty? %))) [commands trace root])
      (throw (ex-info "Run parity test through tests/parity.py" {})))
    (try
      (qt/run-application!
       #(qt/later! 0 (fn [] (exercise! commands trace root)))
       :app-id (System/getenv "SNITT_PARITY_APP_ID")
       :keep-alive false :auto-quit-ms 30000)
      (finally (shutdown-agents)))))
