(ns glimmer-qt.widget
  "Qt controls, padded boxes/flow, and item/overlay/rectangle/spacer primitives.
  Event signatures match glimmer-gtk: :on-change receives text; :on-click and
  :on-activate take no arguments. Handlers are refreshed on every render."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.object :as object]
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
   :margin-left ["Layout.leftMargin" :number 0]
   :margin-right ["Layout.rightMargin" :number 0]
   :margin-top ["Layout.topMargin" :number 0]
   :margin-bottom ["Layout.bottomMargin" :number 0]
   :min-width ["Layout.minimumWidth" :number 0]
   :min-height ["Layout.minimumHeight" :number 0]
   :preferred-width ["Layout.preferredWidth" :number -1]
   :preferred-height ["Layout.preferredHeight" :number -1]
   :max-width ["Layout.maximumWidth" :number 16777215]
   :max-height ["Layout.maximumHeight" :number 16777215]
   :alignment ["Layout.alignment" :value (value/flags "AlignVCenter")]
   :clip ["clip" :bool false]
   :opacity ["opacity" :number 1]
   :z ["z" :number 0]
   :sensitive ["enabled" :bool true]
   :visible ["visible" :bool true]})

(def ^:private padding
  {:padding ["padding" :number 0]
   :padding-left ["leftPadding" :number -1]
   :padding-right ["rightPadding" :number -1]
   :padding-top ["topPadding" :number -1]
   :padding-bottom ["bottomPadding" :number -1]})

;; Position against the owned parent, never a global ID or an unstable pointer.
;; Keep these bindings on primitives; controls compose inside an :item/:overlay.
(def ^:private geometry
  {:x ["offsetX" :number 0] :y ["offsetY" :number 0]
   :width ["explicitWidth" :number -1] :height ["explicitHeight" :number -1]
   :relative-x ["relativeX" :number 0] :relative-y ["relativeY" :number 0]
   :relative-width ["relativeWidth" :number -1]
   :relative-height ["relativeHeight" :number -1]
   :anchor ["anchorMode" :string "none"]
   :implicit-width ["implicitWidth" :number 0]
   :implicit-height ["implicitHeight" :number 0]})

;; Shared text and accessibility contract; enum values use value/enum.
(def ^:private typography
  {:font-family ["font.family" :string ""]
   :font-size ["font.pixelSize" :number 14]
   :font-weight ["font.weight" :number 400]
   :font-italic ["font.italic" :bool false]})
(def ^:private accessible
  {:accessible-name ["accessibleName" :string ""]
   :accessible-description ["Accessible.description" :string ""]
   :tooltip ["tooltip" :string ""]})
(def ^:private text-layout
  {:text-alignment ["horizontalAlignment" :value (value/enum "AlignLeft")]
   :wrap ["wrapMode" :value (value/enum "Wrap")]
   :elide ["elide" :value (value/enum "ElideNone")]})
(def ^:private actions
  (merge common typography accessible
         {:label ["text" :string ""] :highlighted ["highlighted" :bool false]
          :checked ["checked" :bool false] :checkable ["checkable" :bool false]
          :icon ["iconSource" :string ""]
          :icon-only ["iconOnly" :bool false]
          :mode ["mode" :string "normal"]
          :accent ["Material.accent" :color "#91bff0"]
          :theme ["Material.theme" :value (value/enum "Dark")]
          :content-source ["contentSource" :string ""]
          :background-source ["backgroundSource" :string ""]}))

(defonce ^:private registry (atom {}))

(defn register!
  "Register a tag before starting a session. :source is an absolute QML URL
  (qrc:/ or file:/) or native:factory-name. :props maps keys to [Qt-name type
  default], with :string/:number/:bool/:color/:value types. :events maps handler keys
  to :none or :string payloads. :required lists props required on every render.
  :children is :none (default), :self, or an owned QObject property name.
  Every tag accepts :qt/subscriptions; see glimmer-qt.object/reconcile!.
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
    (when-not (and (keyword? key) (not= key :qt/subscriptions) (vector? entry) (= 3 (count entry))
                  (string? (first entry)) (seq (first entry))
                  (#{:string :number :bool :color :value} (second entry)))
      (throw (ex-info "Invalid Qt property schema." {:property key}))))
  (doseq [[key arity] events]
    (when-not (and (keyword? key) (not (#{:destroyed :shutdown :close-request :work :work-canceled :signal :signal-error :subscription-ended :qt/subscriptions} key))
                   (#{:none :string} arity) (not (contains? props key)))
      (throw (ex-info "Invalid Qt event schema." {:event key}))))
  (when-not (every? #(contains? props %) required)
    (throw (ex-info "Required Qt properties must have schemas." {})))
  (when (contains? @registry tag) (throw (ex-info "Qt tag already registered." {:tag tag})))
  (swap! registry assoc tag (assoc descriptor :props props :events events :required required
                                  :children children :relay relay))
  tag)

(doseq [[tag props events children relay]
        [[:vbox (assoc (merge common padding) :spacing ["spacing" :number 0]) {} "content" ""]
         [:hbox (assoc (merge common padding) :spacing ["spacing" :number 0]) {} "content" ""]
         [:flow (assoc (merge common padding) :spacing ["spacing" :number 0]) {} "content" ""]
         [:item (merge common geometry) {} :self ""]
         [:overlay (merge common geometry) {} :self ""]
         [:spacer (merge common geometry) {} :none ""]
         [:rectangle (merge common geometry {:color ["color" :color "transparent"]
                                            :radius ["radius" :number 0]
                                            :border-width ["border.width" :number 0]
                                            :border-color ["border.color" :color "transparent"]}) {} :self ""]
         [:label (merge common typography accessible text-layout
                        {:label ["text" :string ""] :color ["color" :color "#ffffff"]}) {} :none ""]
         [:entry (merge common {:text ["text" :string ""] :placeholder ["hint" :string ""]})
          {:on-change :string :on-activate :none} :none "relay"]
         [:button actions
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

(defn- prop-value [props key default]
  (get props key (if (#{:margin-left :margin-right :margin-top :margin-bottom} key)
                   (get props :margin 0) default)))

(defn- validate-props! [tag props]
  (when (and (= tag :button) (contains? props :mode) (not (#{"normal" "good" "bad"} (:mode props))))
    (throw (ex-info "Action mode must be normal, good or bad." {})))
  (when (and (= tag :button) (:icon-only props) (empty? (:accessible-name props)))
    (throw (ex-info "Icon-only actions require an accessible name." {})))
  (object/validate-subscriptions! (:qt/subscriptions props))
  (when (and (contains? props :anchor) (not (#{"none" "fill" "center"} (:anchor props))))
    (throw (ex-info "Unsupported anchor; use none, fill or center relative to parent." {})))
  (doseq [[a b] [[:width :relative-width] [:height :relative-height]]]
    (when (and (contains? props a) (contains? props b))
      (throw (ex-info "Explicit and relative dimensions conflict." {:properties [a b]}))))
  (when (and (= "fill" (:anchor props))
             (some #(contains? props %) [:x :y :width :height :relative-x :relative-y :relative-width :relative-height]))
    (throw (ex-info "Fill anchor owns position and dimensions." {})))
  (doseq [[lo hi] [[:min-width :max-width] [:min-height :max-height]]]
    (when (and (number? (get props lo)) (number? (get props hi)) (> (get props lo) (get props hi)))
      (throw (ex-info "Minimum dimension exceeds maximum." {}))))
  (doseq [key [:width :height :relative-width :relative-height :implicit-width :implicit-height
               :min-width :min-height :preferred-width :preferred-height :max-width :max-height
               :padding :padding-left :padding-right :padding-top :padding-bottom :spacing
               :radius :border-width]
          :let [v (get props key)] :when (and (number? v) (neg? v))]
    (throw (ex-info "Geometry dimensions and spacing must be nonnegative; omit to use defaults." {:property key})))
  (when (and (number? (:opacity props)) (not (<= 0 (:opacity props) 1)))
    (throw (ex-info "Opacity must be between zero and one." {})))
  (let [spec (spec-for tag)]
    (doseq [key (:required (registration tag))]
      (when-not (contains? props key)
        (throw (ex-info "Missing required Qt property." {:tag tag :property key}))))
    (doseq [[key value] (dissoc props :qt/subscriptions)]
      (when-not (contains? spec key)
        (throw (ex-info "Unsupported glimmer-qt property." {:tag tag :property key})))
      (if-let [[_ type] (get spec key)]
        (when-not (case type :string (string? value) :color (string? value) :number (number? value) :bool (boolean? value)
                        :value (do (value/wire value) true))
          (throw (ex-info "Invalid Qt property value." {:property key :value value})))
        (when (and value (not (ifn? value)))
          (throw (ex-info "Qt event handler must be callable." {:property key})))))))

(defn- validate-placement! [parent-tag props]
  (when (and (#{:window :hbox :vbox :flow} parent-tag)
             (or (some #(contains? props %) [:x :y :relative-x :relative-y :relative-width :relative-height :anchor])
                 (and (not= :flow parent-tag) (some #(contains? props %) [:width :height]))))
    (throw (ex-info "Layout owns child geometry; use preferred dimensions and an overlay container." {})))
  (when (and (#{:flow :item :overlay :rectangle} parent-tag)
             (some #(contains? props %) [:hexpand :vexpand :min-width :min-height :max-width :max-height
                                         :preferred-width :preferred-height :alignment :margin :margin-left
                                         :margin-right :margin-top :margin-bottom]))
    (throw (ex-info "This parent does not support Layout hints; use intrinsic sizes or a nested box." {}))))

(defn apply-props! [tag widget props]
  (let [spec (spec-for tag)
        previous (:props (tagged-widget tag widget))]
    ;; Validate all keys and managed shapes first; each native write also checks
    ;; Qt metadata and ranges before mutating that property.
    (validate-props! tag props)
    (validate-placement! (:tag (get @widgets (:parent (get @widgets widget)))) props)
    (doseq [[key entry] spec :when entry]
      (let [[property type default] entry
            value (prop-value props key default)]
        (when (or (nil? previous) (#{:text :checked} key) (not= value (prop-value previous key default)))
          (q/set-controlled-property! widget property (if (= type :color) (value/color value) value)))))
    (swap! widgets assoc-in [widget :props] props)
    (object/reconcile! widget (:qt/subscriptions props)))
  nil)

(defn- forget-tree! [widget]
  (when-let [data (get @widgets widget)]
    (let [failure (atom nil)]
      ;; Release every descendant even when an application hook throws.
      (doseq [child (:children data)]
        (try (forget-tree! child) (catch Throwable e (reset! failure e))))
      (try (object/disconnect-owner! widget) (catch Throwable e (reset! failure e)))
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
                              (let [v (prop-value props key default)]
                                [property (if (= type :color) (value/color v) v)])) schema))
        widget (q/create-registered @session source (value/encode initial) relay)]
    (swap! widgets assoc widget {:tag tag :children [] :parent nil :props props})
    (try
      (when-not (= children :none)
        (swap! widgets assoc-in [widget :target]
               (q/child-target widget (if (= children :self) "" children))))
      (object/reconcile! widget (:qt/subscriptions props))
      widget
      (catch Throwable error (dispose! widget) (throw error)))))

(defn register-window! [widget]
  (when-not (q/alive? widget) (throw (ex-info "Invalid Qt mount point." {})))
  (swap! widgets assoc widget {:tag :window :children [] :parent nil :target widget})
  widget)

(defn forget-window!
  "Release managed mount-point bookkeeping before native window disposal."
  [widget]
  (forget-tree! widget))

(defn- check-container! [tag parent]
  (tagged-widget tag parent)
  (when-not (:target (live-widget parent))
    (throw (ex-info "Qt element cannot contain children." {:tag tag}))))

(defn- check-child! [parent child]
  (when-not (= parent (:parent (live-widget child)))
    (throw (ex-info "Qt widget is not a child of this parent." {:child child :parent parent}))))

(defn- placement! [tag child]
  (validate-placement! tag (:props (live-widget child)))
  (when (#{:item :overlay :rectangle :spacer} (:tag (live-widget child)))
    (q/set-property! child "positionManaged" (boolean (#{:window :hbox :vbox :flow} tag)))
    (q/set-property! child "sizeManaged" (boolean (#{:window :hbox :vbox} tag)))))

(defn append-child! [tag parent child]
  (check-container! tag parent)
  (when (:parent (live-widget child))
    (throw (ex-info "Qt child already has a parent." {:child child})))
  (when (and (= tag :window) (seq (:children (live-widget parent))))
    (throw (ex-info "A Qt window accepts one root component." {})))
  (placement! tag child)
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
    (placement! tag new-child)
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
  "Find a live handle, optionally scoped to a window's mount point. With no
  scope, duplicate IDs are rejected rather than returning another editor's object."
  ([id] (find-by-id nil id))
  ([container id]
   (let [within? (fn [widget]
                   (or (nil? container)
                       (loop [current widget]
                         (cond (= current container) true
                               (nil? current) false
                               :else (recur (:parent (get @widgets current)))))))
         matches (keep (fn [[widget data]]
                         (when (and (= id (get-in data [:props :id])) (within? widget)) widget))
                       @widgets)]
     (when (next matches) (throw (ex-info "Ambiguous Qt object ID; specify a window." {:id id})))
     (first matches))))

(defn live-count [] (count @widgets))

(defn clear! []
  (let [failure (atom nil)]
    (doseq [widget (keys @widgets)]
      (try (forget-tree! widget) (catch Throwable e (reset! failure e))))
    (reset! widgets {})
    (reset! session nil)
    (when @failure (throw @failure))))
