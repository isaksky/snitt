(ns glimmer.core
  "The component model and reconciler for glimmer.

  Components return hiccup — vectors of [:tag props? children...] where children
  may themselves be native elements ([keyword ...]) or component invocations
  ([fn args...]). Strings/numbers become labels. nil children are skipped.

  Two component shapes, as in reagent:
    Form-1: (defn greeting [name] [:label {:label (str \"hi \" name)}])
            — called fresh on every render.
    Form-2: (defn counter []
              (let [n (atom 0)]            ; local state, created once
                (fn [] [:button {:label (str @n) :on-click #(swap! n inc)}])))
            — the outer fn runs once (on mount); the returned fn renders.

  Reconciling is positional: at each child position the existing widget is reused
  when the tag matches (props re-applied) and replaced when it doesn't; surplus
  children are removed and new ones appended. Components are transparent — they
  produce no widget of their own, just expand to native elements — but each one
  binds glimmer.ratom/*current-watcher* around its render so any reactive cell it
  derefs re-runs that component (and only that component) on change.

  This namespace is toolkit-independent. Everything native — what a widget is,
  how props are applied, how children are attached, what the event loop looks
  like — is reached through glimmer.backend, so the same reconciler drives GTK
  (glimmer-gtk), and any Qt or TUI backend that implements the same map of
  functions."
  (:require [glimmer.backend :as b]
            [glimmer.ratom :as r]))

;; --- reactive re-render marshalling (live REPL development) ------------------
;; While an app runs, the backend's event loop owns the main thread. notify! runs
;; a changed cell's watchers synchronously on the caller's thread, so a ratom
;; mutation made off the main thread (an nREPL eval on its worker thread, or any
;; future) would otherwise reconcile — calling into the toolkit — off the main
;; thread, which most toolkits reject (AppKit does, on macOS). While the loop
;; runs we defer each component's re-render onto it via backend/schedule.
;; Headless, with no loop running, re-renders stay synchronous exactly as before.

;; The running app's mounted root, recorded by run so reload! can re-render it in
;; the same window (REPL hot-reload). Holds the current run's root-state atom,
;; or nil when no app is running. Backend lifecycle callbacks own that atom.
(defonce ^:private live-root (atom nil))

(defn on-gui
  "Run zero-arg `work` on the UI thread — asynchronously, on the next iteration
  of the event loop — while an app is running. Use it from the nREPL REPL (or any
  off-main-thread code) to safely touch widgets directly (e.g. re-mount a root
  after redefining its component), since toolkits reject off-main-thread widget
  mutation. Reactive updates don't need this: a plain swap!/reset! on a ratom the
  UI derefs already re-renders on the UI thread. Runs `work` inline when no loop
  is running (so it is usable headless and in tests)."
  [work]
  (if (not @b/loop-running?) (work) (b/schedule work)))

(defn make-rerender-watcher
  "Return the per-component reactive watcher that re-runs `render` when a cell it
  derefs changes. When `running?` (an atom) is truthy the change is posted onto
  the UI thread via `schedule` (work->any) instead of rendered inline, and a
  pending flag coalesces a burst of changes into a single deferred render.
  `running?`/`schedule` default to the live backend so a running app marshals
  while headless tests render inline; they are exposed as args so the decision is
  unit-testable without a toolkit.

  `disposed` (an atom) is set true when the component is unmounted. Once disposed
  the watcher never renders again (its widgets are gone) and, on the next change,
  removes itself from the notifying cell. Component subscription scopes also
  detach every tracked cell immediately at disposal."
  ([render] (make-rerender-watcher render b/loop-running? b/schedule (atom false)))
  ([render running? schedule] (make-rerender-watcher render running? schedule (atom false)))
  ([render running? schedule disposed]
   (let [pending (atom false)
         self (atom nil)]
     (reset! self
             (fn [cell]
               (if @disposed
                 (r/unwatch! cell @self)
                 (if (not @running?)
                   (render)
                   (when (compare-and-set! pending false true)
                     (schedule (fn [] (reset! pending false) (when-not @disposed (render)))))))))
     @self)))

;; --- hiccup parsing ----------------------------------------------------------
(defn- parse-native [v]
  (let [tag (first v) body (next v)
        props (when (and (seq body) (map? (first body))) (first body))]
    (if props
      {:tag tag :props (dissoc props :key) :children (vec (rest body))}
      {:tag tag :props {} :children (vec body)})))

;; A parent's child forms can mix elements, nil holes (`(when cond ...)`) and
;; spliced seqs (`(for ...)`, list). Normalize to a flat vector of elements
;; before positional diffing: splice (possibly nested) seqs, drop nils. A bare
;; vector is NOT spliced — it is itself one child element (standard hiccup).
(defn- walk-child
  [acc x]
  (cond
    (nil? x)  acc
    (seq? x)  (reduce walk-child acc x)
    :else     (conj acc x)))

(defn flatten-children
  "Normalize a parent's child forms into a flat vector of hiccup elements.
  Splices (possibly nested) seqs and drops nils, so `(for [t tasks] [row t])`
  renders one widget per task and `(when cond [...])` leaves no hole. Bare
  vectors are kept as single children."
  [xs]
  (reduce walk-child [] xs))

;; --- keyed children (pure planning) ------------------------------------------
;; Keyed reconciliation matches children by a stable :key instead of by position,
;; so list items can be added, removed or reordered without a handler capturing a
;; stale index. The planning below is pure (operates on keys, not widgets) so it
;; is unit-testable headlessly; the widget wiring lives in reconcile-children!.

(defn child-key
  "The stable identity of a child element used by keyed reconciliation, or nil
  when the child is unkeyed. Taken from :key in the element's props map (a native
  [:tag {:key k}] or a component invocation [f {:key k} ...]) or, failing that,
  from ^{:key k} metadata on the vector — mirroring reagent. Non-vector children
  (strings, numbers, nil) are never keyed."
  [el]
  (when (vector? el)
    (or (-> el meta :key)
        (let [body (next el)]
          (when (and (seq body) (map? (first body)))
            (:key (first body)))))))

(defn keyed?
  "True when every child in `children` (a flat list of hiccup elements) carries a
  non-nil key. An empty list is not keyed — the positional path handles the
  no-children case. Mixed keyed/unkeyed lists fall back to positional so an
  accidental missing key never silently mis-reconciles."
  [children]
  (boolean (and (seq children)
                (every? #(some? (child-key %)) children))))

(defn- strictly-increasing? [xs]
  ;; `(apply < [])` throws in jolt (< needs >=1 arg), so treat empty as vacuously
  ;; increasing — a list with no reused widgets needs no reorder.
  (or (empty? xs) (apply < xs)))

(defn keyed-plan
  "Pure diff of old vs new child keys -> a reconcile plan the keyed path applies
  against real widgets. `old-keys`/`new-keys` are the keys of the previously-
  mounted and newly-requested children, in order. Returns a map:
    :destroy  [old-index ...]                                keys gone from new
    :slots    [{:key, :kind :reuse|:create, :old-index?}]    in target order
    :reorder? boolean                                         widget reorder needed
  :reorder? is false when surviving widgets already sit in the right relative
  order with new ones appended after them (containers append to the tail); true
  otherwise — e.g. survivors were reordered, or a new item must appear before an
  existing one."
  [old-keys new-keys]
  (let [old-idx  (into {} (map-indexed (fn [i k] [k i]) old-keys))
        new-set  (set new-keys)
        destroy  (vec (for [[i k] (map-indexed vector old-keys)
                            :when (not (new-set k))] i))
        slots    (vec (for [k new-keys]
                        (if-let [oi (old-idx k)]
                          {:key k :kind :reuse :old-index oi}
                          {:key k :kind :create})))
        reuse-old (vec (keep :old-index (filter #(= :reuse (:kind %)) slots)))
        create-pos (keep-indexed (fn [i s] (when (= :create (:kind s)) i)) slots)
        reuse-pos  (keep-indexed (fn [i s] (when (= :reuse (:kind s)) i)) slots)
        create-before-reuse (and (seq create-pos) (seq reuse-pos)
                                 (< (apply min create-pos) (apply max reuse-pos)))
        reorder? (boolean (or (not (strictly-increasing? reuse-old)) create-before-reuse))]
    {:destroy destroy :slots slots :reorder? reorder?}))

;; --- instance tree -----------------------------------------------------------
;; Each rendered element has an atom holding its instance state:
;;   native: {:type :native :tag :button :props {} :widget <w> :children [atom...]}
;;   comp:   {:type :comp :render-fn <fn> :children [atom]}  ; one child = expanded root
;; A nil instance means nothing has been mounted at that position yet.

(declare reconcile-el!)

(defn- inst-widget
  "The widget an instance contributes to its parent container: a native
  instance's own :widget, or — for a component, which owns no widget — the widget
  of its single expanded child (recursing, since a component can expand to another
  component). nil if nothing is mounted yet."
  [inst-atom]
  (when inst-atom
    (let [inst @inst-atom]
      (or (:widget inst)
          (some-> (first (:children inst)) inst-widget)))))

(defn- destroy-inst!
  "Remove the widget an instance contributes from `parent-widget`. A native
  instance owns its widget: removing it unparents the whole subtree, so we stop —
  recursing into its children would ask an already-finalized container to remove
  children it no longer has (the source of GTK_IS_BOX criticals under GTK). A
  component owns no widget, so its expanded children are parented in the
  component's own container; recurse with the same parent to remove them from
  there."
  [inst-atom parent-widget parent-tag]
  (when inst-atom
    (let [inst @inst-atom
          widget (:widget inst)]
      (if widget
        (b/remove-child! parent-tag parent-widget widget)
        (doseq [child (:children inst)]
          (destroy-inst! child parent-widget parent-tag))))))

(defn- dispose-tree!
  "Mark every component watcher in the instance subtree disposed, so it stops
  rendering and immediately detaches its reactive subscriptions. Pure
  bookkeeping (no widget calls), so unlike destroy-inst! it recurses the whole
  tree regardless of which instances own widgets. Called wherever a subtree is
  removed (unmount, surplus/removed children) so subscriptions don't outlive the
  widgets — which matters most when re-mounting against a long-lived (defonce)
  cell."
  [inst-atom]
  (when inst-atom
    (let [inst @inst-atom]
      (when-let [d (:disposed inst)] (reset! d true))
      (r/clear-subscriptions! (:subscription-scope inst))
      (doseq [child (:children inst)]
        (dispose-tree! child)))))

(defn- reconcile-positional-children!
  "Positionally reconcile `new-children` (a flattened vector of hiccup) against the
  instance's existing children, reusing widgets where the element shape matches at
  a position. The default path when children are not uniformly keyed."
  [parent-widget parent-tag new-children inst-atom]
  (let [old (:children @inst-atom)
        n (count new-children)
        ;; reuse the child atom at each position when it exists; else a fresh one
        reused (vec (for [i (range n)]
                      (if (< i (count old)) (nth old i) (atom nil))))]
    ;; remove widgets for children that no longer exist
    (doseq [i (range n (count old))]
      (dispose-tree! (nth old i))
      (destroy-inst! (nth old i) parent-widget parent-tag))
    ;; Publish slots before rendering: a failed sibling must still be disposable.
    (swap! inst-atom assoc :children reused)
    ;; reconcile each child into its (possibly new) atom
    (doseq [i (range n)]
      (reconcile-el! parent-widget parent-tag (nth new-children i) (nth reused i)))
    (swap! inst-atom assoc :children reused)))

(defn- reorder-keyed-widgets!
  "Force `child-atoms`' widgets into the given order within the parent, moving
  each widget to sit after the previous one (the first goes to position 0).
  Called only when keyed-plan says a reorder is needed. Repositioning does not
  recreate widgets, so signal handlers and the instance atoms stay intact. A
  no-op for backends without ordered containers."
  [parent-widget parent-tag child-atoms]
  ;; resolve each child to its contributed widget — a keyed child may be a
  ;; component, whose widget lives one level down on its expanded child.
  (let [widgets (vec (map inst-widget child-atoms))
        n (count widgets)]
    (when (pos? n)
      (doseq [i (range n)]
        (let [w (nth widgets i)
              prev (when (pos? i) (nth widgets (dec i)))]
          (b/reorder-child! parent-tag parent-widget w prev))))))

(defn- reconcile-keyed-children!
  "Reconcile uniformly-keyed `new-children` (a flattened vector of hiccup, each
  carrying a non-nil :key) by stable identity rather than position. Derives a
  reuse/create/reorder plan from keyed-plan, destroys removed children, reuses
  surviving atoms by key, creates fresh atoms for new keys, re-applies each
  child's hiccup into its atom, and fixes widget order when the plan requires it.
  Stores the child's :key on its instance so the next render can read old keys
  back — that is how a row's widget and signal handlers follow the item across
  add/remove/reorder instead of a stale position."
  [parent-widget parent-tag new-children inst-atom]
  (let [old (:children @inst-atom)
        ;; raw old keys keep nils on purpose: a nil-keyed legacy child (mounted
        ;; before this parent became keyed) is absent from new-set, so keyed-plan
        ;; destroys it rather than orphaning its widget in the container.
        old-keys (vec (map (fn [a] (:key @a)) old))
        new-keys (vec (map child-key new-children))
        plan (keyed-plan old-keys new-keys)
        destroy (:destroy plan)
        slots (:slots plan)
        reorder? (:reorder? plan)
        ;; reuse lookups are by key; nil-keyed old children are never reused
        old-by-key (into {} (keep (fn [a] (when-let [k (:key @a)] [k a])) old))]
    (doseq [i destroy]
      (dispose-tree! (nth old i))
      (destroy-inst! (nth old i) parent-widget parent-tag))
    (let [child-atoms (vec (for [slot slots]
                             (if (= :reuse (:kind slot))
                               (get old-by-key (:key slot))
                               (atom nil))))]
      (swap! inst-atom assoc :children child-atoms)
      (doseq [i (range (count child-atoms))]
        (let [a (nth child-atoms i)]
          (reconcile-el! parent-widget parent-tag (nth new-children i) a)
          (swap! a assoc :key (nth new-keys i))))
      (when reorder?
        (reorder-keyed-widgets! parent-widget parent-tag child-atoms))
      (swap! inst-atom assoc :children child-atoms))))

(defn- reconcile-children!
  "Reconcile `new-children` (hiccup) against the instance's existing children.
  When every child carries a stable :key, reconcile by identity — a row's widget
  and signal handlers follow its key across add/remove/reorder, so a handler may
  safely close over per-item state. Otherwise fall back to positional matching.
  `new-children` is flattened first."
  [parent-widget parent-tag new-children inst-atom]
  (let [flat (flatten-children new-children)]
    (if (keyed? flat)
      (reconcile-keyed-children! parent-widget parent-tag flat inst-atom)
      (reconcile-positional-children! parent-widget parent-tag flat inst-atom))))

(defn- reconcile-native!
  [parent-widget parent-tag v inst-atom]
  (let [{:keys [tag props children]} (parse-native v)
        inst @inst-atom]
    (if (and inst (= (:type inst) :native) (= (:tag inst) tag))
      (do (b/apply-props! tag (:widget inst) props)
          (swap! inst-atom assoc :props props))
      (let [widget (b/create! tag props)
            new {:type :native :tag tag :props props :widget widget :children []}]
        (try
          (if (and inst (= (:type inst) :native))
            (do
              ;; native -> native of another tag: the old widget goes, and the
              ;; component watchers in its subtree must go with it, or they keep
              ;; rendering into a widget that is no longer in the tree.
              (dispose-tree! inst-atom)
              (b/replace-child! parent-tag parent-widget (:widget inst) widget))
            (do
              ;; comp -> native: the component owned no widget, but its expanded
              ;; children are parented here and its watchers are live. Tear both
              ;; down or the old subtree keeps rendering beside the new widget.
              (when inst
                (dispose-tree! inst-atom)
                (destroy-inst! inst-atom parent-widget parent-tag))
              (b/append-child! parent-tag parent-widget widget)))
          (catch Throwable error
            (b/dispose-widget! widget)
            (throw error)))
        (reset! inst-atom new)))
    (reconcile-children! (:widget @inst-atom) tag children inst-atom)))

(defn rerender-thunk
  "The zero-arg closure a component's watcher runs to re-render it: re-reconciles
  the instance's last stored invocation (:v). No-ops when the instance no longer
  holds one — a fresh slot, a reset skeleton, or an unmounted tree. A stale
  watcher firing after its instance was reset used to re-render (:v nil) = nil,
  which reconcile-comp! read as a component invocation [nil] and crashed invoking
  nil. `reconcile` is passed in so the guard is testable without a backend."
  [reconcile parent-widget parent-tag inst-atom]
  (fn [] (when-let [v (:v @inst-atom)]
           (reconcile parent-widget parent-tag v inst-atom))))

(defn- reconcile-comp!
  "Expand a component invocation into native hiccup and reconcile its single child.
  Each component renders exactly once per render: a Form-2 component's outer fn
  runs only on mount (its returned render fn is cached); a Form-1 component's fn
  is re-applied each render. We classify with fn?, not ifn? — a hiccup vector is
  callable, so ifn? would treat every Form-1 result as a Form-2 render fn."
  [parent-widget parent-tag v inst-atom]
  (let [f (first v)
        ;; strip a leading {:key k} so it never reaches the component fn — mirrors
        ;; parse-native. A key-only map drops entirely; a map mixing :key with real
        ;; props drops just :key. Non-map / key-less leading args are untouched.
        args (let [raw (rest v)]
               (if (and (seq raw) (map? (first raw)) (contains? (first raw) :key))
                 (let [m (dissoc (first raw) :key)]
                   (if (seq m) (cons m (rest raw)) (rest raw)))
                 raw))
        cur @inst-atom
        cached-render-fn (when (= f (first (:v cur))) (:render-fn cur))]
    ;; native -> comp: a component has no widget of its own, so a native element
    ;; previously mounted here would be left parented behind the expanded child.
    ;; Reset to a fresh comp skeleton — its native :type/:tag/:widget must NOT
    ;; survive, or a later comp->native transition would mistake the stale :type
    ;; and deref a nil :widget.
    (when (and cur (or (:widget cur) (and (:v cur) (not= f (first (:v cur))))))
      (dispose-tree! inst-atom)
      (destroy-inst! inst-atom parent-widget parent-tag)
      (reset! inst-atom {:children [(atom nil)]}))
    ;; ensure exactly one child slot for the expanded root. Use `seq`, not
    ;; truthiness: a native leaf carries :children [], which is truthy but empty —
    ;; (first []) is nil, and reconcile-el! would deref it (the clear-all crash).
    (swap! inst-atom (fn [c] (if (seq (:children c)) c (assoc c :children [(atom nil)]))))
    ;; stash the current invocation so the watcher always uses fresh args.
    ;; Without this a Form-1 child that subscribes to its own reactives
    ;; re-renders with the initial v from mount, overwriting parent updates.
    (swap! inst-atom assoc :v v :type :comp)
    (let [child-atom (first (:children @inst-atom))
          ;; subscribe: any reactive read during render re-runs just this component.
          ;; The watcher is cached on the instance so it's the SAME fn every render;
          ;; a fresh closure would grow each cell's watch set on every re-render.
          watcher (or (:watcher @inst-atom)
                      ;; Disposal detaches the scope and fences already-queued renders.
                      (let [disposed (atom false)
                            w (make-rerender-watcher
                                (rerender-thunk reconcile-comp! parent-widget parent-tag inst-atom)
                                b/loop-running? b/schedule disposed)]
                        (swap! inst-atom assoc :watcher w :disposed disposed
                               :subscription-scope (r/watch-scope w)) w))
          scope (:subscription-scope @inst-atom)
          _ (r/clear-subscriptions! scope)
          hiccup (binding [r/*current-watcher* watcher r/*watch-scope* scope]
                   (if cached-render-fn
                     (cached-render-fn)                       ; Form-2: cached render fn
                     (let [result (apply f args)]             ; first invocation
                       (if (fn? result)
                         (do (swap! inst-atom assoc :type :comp :render-fn result) (result))
                         result))))]                          ; Form-1: hiccup, used directly
      (reconcile-el! parent-widget parent-tag hiccup child-atom))))

(defn- reconcile-el!
  "Reconcile one hiccup element (native, component, or primitive) into inst-atom."
  [parent-widget parent-tag el inst-atom]
  (cond
    (nil? el) (do (dispose-tree! inst-atom)
                  (destroy-inst! inst-atom parent-widget parent-tag)
                  (reset! inst-atom nil))
    (string? el) (reconcile-native! parent-widget parent-tag (b/text->element el) inst-atom)
    (number? el) (reconcile-native! parent-widget parent-tag (b/text->element (str el)) inst-atom)
    (vector? el)
    (let [head (first el)]
      (cond
        (keyword? head) (reconcile-native! parent-widget parent-tag el inst-atom)
        (fn? head)      (reconcile-comp! parent-widget parent-tag el inst-atom)
        :else (throw (ex-info (str "unsupported hiccup element: " (pr-str el)) {:el el}))))
    :else (reconcile-native! parent-widget parent-tag (b/text->element (pr-str el)) inst-atom)))

;; --- public API --------------------------------------------------------------
(defn mount
  "Mount hiccup into a container. Returns an owned instance atom for unmount!.
  Partial creation failures dispose subscriptions and already-created children."
  [container-widget container-tag hiccup]
  (let [root (atom nil)]
    (try
      (reconcile-el! container-widget container-tag hiccup root)
      root
      (catch Throwable error
        (dispose-tree! root)
        (try (destroy-inst! root container-widget container-tag)
             (catch Throwable _ nil))
        (throw error)))))

(defn unmount!
  "Dispose subscriptions then native widgets. Idempotent for a mounted root."
  [root-inst-atom container-widget container-tag]
  (dispose-tree! root-inst-atom)
  (try (destroy-inst! root-inst-atom container-widget container-tag)
       (finally (reset! root-inst-atom nil))))

(defn run
  "Run a root with the installed backend. Lifecycle-aware backends receive
  explicit :mount!, :unmount! and :abandon! callbacks via :run-lifecycle.
  Callback return values are unspecified. See glimmer.backend's contract."
  [root-fn & {:as opts}]
  (let [state (atom nil)
        dispose! (fn [native?]
                   (when-let [{:keys [window tag inst]} @state]
                     (reset! state nil)
                     (if native?
                       (unmount! inst window tag)
                       (do (dispose-tree! inst) (reset! inst nil)))))
        lifecycle {:mount! (fn [window tag]
                             (when @state (dispose! true))
                             (reset! state {:window window :tag tag
                                            :inst (mount window tag [root-fn])
                                            :root-fn root-fn})
                             nil)
                   :unmount! #(dispose! true)
                   :abandon! #(dispose! false)}]
    (when-not (compare-and-set! live-root nil state)
      (throw (ex-info "A Glimmer root is already running." {})))
    (try
      (b/run-lifecycle (or opts {}) lifecycle)
      (finally
        ;; Legacy backends may already have destroyed their native container.
        ;; Qt unmounts explicitly, on its GUI thread, before closing the engine.
        (dispose! false)
        (compare-and-set! live-root state nil)))))

(defn reload!
  "Remount the current root on its GUI thread, disposing the previous tree's
  subscriptions immediately. The callback belongs to this run only; queued
  reloads cannot affect a later session. No-op when no root is running."
  ([] (reload! nil))
  ([new-root-fn]
   (when-let [state @live-root]
     (on-gui
       (fn []
         (when (= state @live-root)
           (when-let [{:keys [window tag inst] :as st} @state]
             (let [f (or new-root-fn (:root-fn st))]
               (reset! state nil)
               (unmount! inst window tag)
               (reset! state (assoc st :inst (mount window tag [f]) :root-fn f))))))))
   nil))
