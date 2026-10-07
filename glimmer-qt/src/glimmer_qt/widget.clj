(ns glimmer-qt.widget
  "Registered Qt widgets, including :vbox, :hbox, :label, :entry, :button.
  Event signatures match glimmer-gtk: :on-change receives text; :on-click and
  :on-activate take no arguments. Handlers are refreshed on every render."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.value :as value]))

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

(defonce ^:private registry (atom {}))

(defn register!
  "Register a tag before starting a session. :source is an absolute QML URL
  (qrc:/ or file:/) or native:factory-name. :props maps keys to [Qt-name type
  default], with :string/:number/:bool/:color types. :events maps handler keys
  to :none or :string payloads. :required lists props required on every render.
  :children is :none (default), :self, or an owned QQuickItem property name.
  :relay names an optional QtObject property for relay.dispatch(object,event,text).
  :dispose is an optional zero-argument notification, run once on disposal.
  Registrations are immutable while a session is running. Duplicate tags fail."
  [tag {:keys [source props events required children relay dispose]
        :or {props {} events {} required #{} children :none relay ""} :as descriptor}]
  (when @session (throw (ex-info "Register Qt tags outside a running session." {})))
  (when-not (and (keyword? tag) (not= tag :window)
                (string? source) (re-find #"^(qrc:/|file:/|native:)" source)
                (map? props) (map? events) (set? required)
                (or (#{:none :self} children) (and (string? children) (seq children)))
                (string? relay) (or (nil? dispose) (ifn? dispose)))
    (throw (ex-info "Invalid Qt registration descriptor." {:tag tag})))
  (doseq [[key entry] props]
    (when-not (and (keyword? key) (vector? entry) (= 3 (count entry))
                  (string? (first entry)) (seq (first entry))
                  (#{:string :number :bool :color} (second entry)))
      (throw (ex-info "Invalid Qt property schema." {:property key}))))
  (doseq [[key arity] events]
    (when-not (and (keyword? key) (not (#{:destroyed :shutdown :work :work-canceled} key))
                   (#{:none :string} arity) (not (contains? props key)))
      (throw (ex-info "Invalid Qt event schema." {:event key}))))
  (when-not (every? #(contains? props %) required)
    (throw (ex-info "Required Qt properties must have schemas." {})))
  (when (contains? @registry tag) (throw (ex-info "Qt tag already registered." {:tag tag})))
  (swap! registry assoc tag (assoc descriptor :props props :events events :required required
                                  :children children :relay relay))
  tag)

(doseq [[tag props events children relay]
        [[:vbox (assoc common :spacing ["spacing" :number 0]) {} :self ""]
         [:hbox (assoc common :spacing ["spacing" :number 0]) {} :self ""]
         [:label (merge common {:label ["text" :string ""] :color ["color" :color "#ffffff"]
                                :font-size ["font.pixelSize" :number 14]
                                :font-weight ["font.weight" :number 400]}) {} :none ""]
         [:entry (merge common {:text ["text" :string ""] :placeholder ["hint" :string ""]})
          {:on-change :string :on-activate :none} :none "relay"]
         [:button (merge common {:label ["text" :string ""] :highlighted ["highlighted" :bool false]})
          {:on-click :none} :none "relay"]]]
  (register! tag {:source (str "qrc:/glimmer-qt/" (name tag) ".qml")
                  :props props :events events :children children :relay relay}))

(defn- registration [tag]
  (or (get @registry tag) (throw (ex-info "Unsupported glimmer-qt tag." {:tag tag}))))
(defn- spec-for [tag]
  (let [{:keys [props events]} (registration tag)]
    (merge props (into {} (map (fn [[key _]] [key nil]) events)))))

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
    (doseq [key (:required (registration tag))]
      (when-not (contains? props key)
        (throw (ex-info "Missing required Qt property." {:tag tag :property key}))))
    (doseq [[key value] props]
      (when-not (contains? spec key)
        (throw (ex-info "Unsupported glimmer-qt property." {:tag tag :property key})))
      (if-let [[_ type] (get spec key)]
        (when-not (case type :string (string? value) :color (string? value) :number (number? value) :bool (boolean? value))
          (throw (ex-info "Invalid Qt property value." {:property key :value value})))
        (when (and value (not (ifn? value)))
          (throw (ex-info "Qt event handler must be callable." {:property key})))))))

(defn apply-props! [tag widget props]
  (let [spec (spec-for tag)
        previous (:props (tagged-widget tag widget))]
    ;; Validate all keys and managed shapes first; each native write also checks
    ;; Qt metadata and ranges before mutating that property.
    (validate-props! tag props)
    (doseq [[key entry] spec :when entry]
      (let [[property type default] entry
            value (get props key default)]
        (when (or (nil? previous) (= key :text) (not= value (get previous key default)))
          (case type
            :string (q/set-string widget property value)
            :number (q/set-number widget property value)
            :color (q/set-property! widget property (value/color value))
            :bool (q/set-bool widget property (if value 1 0))))))
    (swap! widgets assoc-in [widget :props] props))
  nil)

(defn- forget-tree! [widget]
  (when-let [data (get @widgets widget)]
    (let [failure (atom nil)]
      ;; Release every descendant even when an application hook throws.
      (doseq [child (:children data)]
        (try (forget-tree! child) (catch Throwable e (reset! failure e))))
      (when-let [parent (:parent data)]
        (swap! widgets update-in [parent :children] #(vec (remove #{widget} %))))
      (swap! widgets dissoc widget)
      (try (when-let [hook (:dispose (get @registry (:tag data)))] (hook))
           (catch Throwable e (reset! failure e)))
      (when @failure (throw @failure)))))

(defn dispose!
  "Dispose an owned widget and its subtree, attached or not. Invalid handles fail."
  [widget]
  (when (= :window (:tag (live-widget widget)))
    (throw (ex-info "Cannot dispose the session mount point." {})))
  ;; Forget handlers before native invalidation can call back into the binding.
  (try (forget-tree! widget) (finally (q/remove-widget widget)))
  nil)

(defn create! [tag props]
  (validate-props! tag props)
  (when-not @session (throw (ex-info "No running Qt session." {})))
  (let [{:keys [source relay children] schema :props} (registration tag)
        initial (into {} (map (fn [[key [property type default]]]
                              (let [v (get props key default)]
                                [property (if (= type :color) (value/color v) v)])) schema))
        widget (q/create-registered @session source (value/encode initial) relay)]
    (swap! widgets assoc widget {:tag tag :children [] :parent nil :props props})
    (try
      (when-not (= children :none)
        (swap! widgets assoc-in [widget :target]
               (q/child-target widget (if (= children :self) "" children))))
      widget
      (catch Throwable error (dispose! widget) (throw error)))))

(defn register-window! [widget]
  (when-not (q/alive? widget) (throw (ex-info "Invalid Qt mount point." {})))
  (swap! widgets assoc widget {:tag :window :children [] :parent nil :target widget})
  widget)

(defn- check-container! [tag parent]
  (tagged-widget tag parent)
  (when-not (:target (live-widget parent))
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
  (q/append (:target (live-widget parent)) child)
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
    (q/reorder (:target (live-widget parent)) child
               (or (last (filter #(= 1 (q/is-item %)) (take pos others))) 0))
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
    (q/append (:target (live-widget parent)) new-child)
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
      (case (get-in @registry [(get-in @widgets [widget :tag]) :events (keyword event)])
        :string (handler value)
        :none (handler)
        nil))))

(defn find-by-id
  "Find a live opaque handle on the GUI thread. It becomes invalid at disposal."
  [id]
  (some (fn [[widget data]] (when (= id (get-in data [:props :id])) widget)) @widgets))

(defn live-count [] (count @widgets))

(defn clear! []
  (let [failure (atom nil)]
    (doseq [widget (keys @widgets)]
      (try (forget-tree! widget) (catch Throwable e (reset! failure e))))
    (reset! widgets {})
    (reset! session nil)
    (when @failure (throw @failure))))
