(ns glimmer-qt.widget
  "The intentionally small widget vocabulary: :vbox, :hbox, :label, :entry, :button.
  Event signatures match glimmer-gtk: :on-change receives text; :on-click and
  :on-activate take no arguments. Handlers are refreshed on every render."
  (:require [glimmer-qt.ffi :as q]))

(defonce session (atom nil))
(defonce ^:private widgets (atom {}))

;; Each property is [QML name, value type, default]. Applying defaults also resets
;; removed props; misspelled or unsupported props fail at the binding boundary.
(def ^:private common
  {:id ["objectName" :string ""]
   :hexpand ["Layout.fillWidth" :bool false]
   :vexpand ["Layout.fillHeight" :bool false]
   :margin ["Layout.margins" :number 0]
   :sensitive ["enabled" :bool true]
   :visible ["visible" :bool true]})

(def ^:private specs
  {:vbox {:spacing ["spacing" :number 0]}
   :hbox {:spacing ["spacing" :number 0]}
   :label {:label ["text" :string ""]
           :color ["color" :string "#ffffff"]
           :font-size ["font.pixelSize" :number 14]
           :font-weight ["font.weight" :number 400]}
   :entry {:text ["text" :string ""]
           :placeholder ["hint" :string ""]
           :on-change nil :on-activate nil}
   :button {:label ["text" :string ""]
            :highlighted ["highlighted" :bool false]
            :on-click nil}})

(defn- spec-for [tag]
  (if-let [spec (get specs tag)]
    (merge common spec)
    (throw (ex-info "Unsupported glimmer-qt tag." {:tag tag}))))

(defn- live-widget [widget]
  (or (and (integer? widget) (q/alive? widget) (get @widgets widget))
      (throw (ex-info "Invalid or disposed Qt widget handle." {:widget widget}))))

(defn- tagged-widget [tag widget]
  (let [data (live-widget widget)]
    (when-not (= tag (:tag data))
      (throw (ex-info "Qt widget tag mismatch." {:tag tag :actual (:tag data)})))
    data))

(defn- validate-props! [tag props]
  (let [spec (spec-for tag)]
    (doseq [[key value] props]
      (when-not (contains? spec key)
        (throw (ex-info "Unsupported glimmer-qt property." {:tag tag :property key})))
      (if-let [[_ type] (get spec key)]
        (when-not (case type :string (string? value) :number (number? value) :bool (boolean? value))
          (throw (ex-info "Invalid Qt property value." {:property key :value value})))
        (when (and value (not (ifn? value)))
          (throw (ex-info "Qt event handler must be callable." {:property key})))))))

(defn apply-props! [tag widget props]
  (let [spec (spec-for tag)
        previous (:props (tagged-widget tag widget))]
    ;; Validate the complete input before performing any writes or changing handlers.
    (validate-props! tag props)
    (doseq [[key entry] spec :when entry]
      (let [[property type default] entry
            value (get props key default)]
        (when (or (nil? previous) (= key :text) (not= value (get previous key default)))
          (case type
            :string (q/set-string widget property value)
            :number (q/set-number widget property (double value))
            :bool (q/set-bool widget property (if value 1 0))))))
    (swap! widgets assoc-in [widget :props] props))
  nil)

(defn- forget-tree! [widget]
  (when-let [data (get @widgets widget)]
    (doseq [child (:children data)] (forget-tree! child))
    (when-let [parent (:parent data)]
      (swap! widgets update-in [parent :children] #(vec (remove #{widget} %))))
    (swap! widgets dissoc widget)))

(defn dispose!
  "Dispose an owned widget and its subtree, attached or not. Invalid handles fail."
  [widget]
  (when (= :window (:tag (live-widget widget)))
    (throw (ex-info "Cannot dispose the session mount point." {})))
  ;; Forget handlers before native invalidation can call back into the binding.
  (forget-tree! widget)
  (q/remove-widget widget)
  nil)

(defn create! [tag props]
  (validate-props! tag props)
  (when-not @session (throw (ex-info "No running Qt session." {})))
  (let [widget (q/create @session (name tag))]
    (swap! widgets assoc widget {:tag tag :children [] :parent nil})
    (try
      (apply-props! tag widget props)
      widget
      (catch Throwable error
        (dispose! widget)
        (throw error)))))

(defn register-window! [widget]
  (when-not (q/alive? widget) (throw (ex-info "Invalid Qt mount point." {})))
  (swap! widgets assoc widget {:tag :window :children [] :parent nil})
  widget)

(defn- check-container! [tag parent]
  (tagged-widget tag parent)
  (when-not (#{:window :vbox :hbox} tag)
    (throw (ex-info "Qt element cannot contain children." {:tag tag}))))

(defn- check-child! [parent child]
  (when-not (= parent (:parent (live-widget child)))
    (throw (ex-info "Qt widget is not a child of this parent." {:child child :parent parent}))))

(defn append-child! [tag parent child]
  (check-container! tag parent)
  (when (:parent (live-widget child))
    (throw (ex-info "Qt child already has a parent." {:child child})))
  (when (and (= tag :window) (seq (:children (live-widget parent))))
    (throw (ex-info "A Qt window accepts one root component." {})))
  (q/append parent child)
  (swap! widgets assoc-in [child :parent] parent)
  (swap! widgets update-in [parent :children] conj child)
  nil)

(defn remove-child! [tag parent child]
  (check-container! tag parent)
  (check-child! parent child)
  (dispose! child))

(defn reorder-child! [tag parent child sibling]
  (check-container! tag parent)
  (check-child! parent child)
  (when sibling
    (check-child! parent sibling)
    (when (= child sibling) (throw (ex-info "A Qt widget cannot follow itself." {}))))
  (let [others (vec (remove #{child} (:children (live-widget parent))))
        pos (if sibling (inc (count (take-while #(not= sibling %) others))) 0)]
    (q/reorder parent child (or sibling 0))
    (swap! widgets assoc-in [parent :children]
           (vec (concat (take pos others) [child] (drop pos others)))))
  nil)

(defn replace-child! [tag parent old-child new-child]
  (check-container! tag parent)
  (check-child! parent old-child)
  (when (:parent (live-widget new-child))
    (throw (ex-info "Replacement Qt widget already has a parent." {})))
  (let [before (take-while #(not= old-child %) (:children (live-widget parent)))
        sibling (last before)]
    ;; Native append validates cycles before the old tree is removed. Temporarily
    ;; having two roots is internal; no event loop or managed handler runs here.
    (q/append parent new-child)
    (swap! widgets assoc-in [new-child :parent] parent)
    (swap! widgets update-in [parent :children] conj new-child)
    (remove-child! tag parent old-child)
    (reorder-child! tag parent new-child sibling))
  nil)

(defn dispatch! [widget event value]
  (if (= event "destroyed")
    (when (contains? @widgets widget)
      (forget-tree! widget)
      ;; The reconciler cannot keep rendering a tree whose native owner vanished.
      ;; Core abandons its subscriptions and closes the remaining session arena.
      (throw (ex-info "Qt destroyed a mounted object." {:widget widget})))
    (when-let [handler (get-in @widgets [widget :props (keyword event)])]
      (if (= event "on-change") (handler value) (handler)))))

(defn find-by-id
  "Find a live opaque handle on the GUI thread. It becomes invalid at disposal."
  [id]
  (some (fn [[widget data]] (when (= id (get-in data [:props :id])) widget)) @widgets))

(defn live-count [] (count @widgets))

(defn clear! []
  (reset! widgets {})
  (reset! session nil))
