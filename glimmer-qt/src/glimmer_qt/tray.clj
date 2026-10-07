(ns glimmer-qt.tray
  "Owned native tray/menu actions for one Qt application session. Mutations run
  on the GUI thread; callback closures are held only while their tray is live."
  (:require [clojure.string :as str]
            [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as w]))

(defonce ^:private trays (atom {}))
(defonce ^:private actions (atom {}))

(defn- gui! []
  (when-not (= 1 (q/gui-thread))
    (throw (ex-info "Qt tray operation requires the main thread." {}))))
(defn- string! [value field]
  (when-not (and (string? value) (not (str/includes? value "\u0000")))
    (throw (ex-info "Invalid Qt tray text." {:field field})))
  value)
(defn- callback! [value field]
  (when-not (or (nil? value) (ifn? value))
    (throw (ex-info "Qt tray callback must be callable." {:field field})))
  value)
(defn- tray! [tray]
  (gui!)
  (or (get @trays tray)
      (throw (ex-info "Invalid or disposed Qt tray handle." {:tray tray}))))
(defn- action! [action]
  (gui!)
  (or (get @actions action)
      (throw (ex-info "Invalid or disposed Qt tray action handle." {:action action}))))

(defn available?
  "Whether this desktop currently exposes a system tray."
  []
  (gui!)
  (= 1 (q/tray-available)))

(defn create!
  "Create an owned tray and menu. Icon is a local image path. Call show! to
  report missing desktop tray support before relying on background operation."
  [icon & {:keys [tooltip on-activate on-message-click]
           :or {tooltip ""}}]
  (gui!)
  (when-not @w/session (throw (ex-info "No running Qt application." {})))
  (when-not (and (string? icon) (seq icon) (not (str/includes? icon "\u0000")))
    (throw (ex-info "Qt tray icon requires a local image path." {})))
  (string! tooltip :tooltip)
  (callback! on-activate :on-activate)
  (callback! on-message-click :on-message-click)
  (let [tray (q/tray-create @w/session icon tooltip)]
    (swap! trays assoc tray {:actions #{} :on-activate on-activate
                            :on-message-click on-message-click})
    tray))

(defn show! [tray] (tray! tray) (q/tray-show tray 1) nil)
(defn hide! [tray] (tray! tray) (q/tray-show tray 0) nil)
(defn set-icon! [tray path]
  (tray! tray)
  (when-not (and (string? path) (seq path) (not (str/includes? path "\u0000")))
    (throw (ex-info "Qt tray icon requires a local image path." {})))
  (q/tray-set-icon tray path)
  nil)
(defn set-tooltip! [tray tooltip]
  (tray! tray)
  (q/tray-set-tooltip tray (string! tooltip :tooltip))
  nil)
(defn set-handlers!
  "Replace activation and notification handlers without reconnecting signals."
  [tray {:keys [on-activate on-message-click] :as opts}]
  (tray! tray)
  (when-not (every? #{:on-activate :on-message-click} (keys opts))
    (throw (ex-info "Unsupported Qt tray handler." {})))
  (doseq [[key value] opts] (callback! value key))
  (swap! trays update tray merge opts)
  nil)

(defn- validate-action! [opts]
  (when-not (every? #{:label :shortcut-hint :enabled :checkable :checked :on-trigger} (keys opts))
    (throw (ex-info "Unsupported Qt tray action option." {})))
  (string! (:label opts) :label)
  (string! (:shortcut-hint opts) :shortcut-hint)
  (doseq [key [:enabled :checkable :checked]]
    (when-not (boolean? (get opts key))
      (throw (ex-info "Qt tray action state must be boolean." {:field key}))))
  (callback! (:on-trigger opts) :on-trigger)
  (when (and (:checked opts) (not (:checkable opts)))
    (throw (ex-info "Checked Qt tray action must be checkable." {})))
  opts)

(defn- native-action! [action {:keys [label shortcut-hint enabled checkable checked]}]
  (q/tray-update-action action label shortcut-hint
                        (if enabled 1 0) (if checkable 1 0) (if checked 1 0)))

(defn add-action!
  "Append a native action. :shortcut-hint is display text, not a registered
  shortcut. The callback takes no arguments and is replaceable in place."
  [tray & {:as opts}]
  (tray! tray)
  (let [data (validate-action! (merge {:label "" :shortcut-hint ""
                                      :enabled true :checkable false :checked false
                                      :on-trigger nil} opts))
        action (q/tray-add-action tray (:label data) (:shortcut-hint data)
                                  (if (:enabled data) 1 0)
                                  (if (:checkable data) 1 0)
                                  (if (:checked data) 1 0) 0)]
    (swap! actions assoc action (assoc data :tray tray))
    (swap! trays update-in [tray :actions] conj action)
    action))

(defn add-separator! [tray]
  (tray! tray)
  (q/tray-add-action tray "" "" 0 0 0 1))

(defn update-action!
  "Update text, state or callback in place; unrelated QAction identities and
  connections are untouched."
  [action & {:as changes}]
  (let [old (action! action)
        data (validate-action! (merge (dissoc old :tray) changes))]
    (when (some #(contains? changes %) [:label :shortcut-hint :enabled :checkable :checked])
      (native-action! action data))
    (swap! actions assoc action (assoc data :tray (:tray old)))
    nil))

(defn notify!
  "Show a desktop notification. Kind is :none, :information, :warning or
  :critical. Throws when this desktop does not support tray messages."
  [tray title body & {:keys [kind timeout-ms]
                     :or {kind :information timeout-ms 10000}}]
  (tray! tray)
  (string! title :title)
  (string! body :body)
  (when-not (and (contains? #{:none :information :warning :critical} kind)
                 (integer? timeout-ms) (<= 0 timeout-ms 2147483647))
    (throw (ex-info "Invalid Qt tray notification." {})))
  (q/tray-notify tray title body
                 ({:none 0 :information 1 :warning 2 :critical 3} kind) timeout-ms)
  nil)

(defn destroy!
  "Hide and synchronously dispose the menu, actions, icon and closures."
  [tray]
  (when-let [data (get @trays tray)]
    (gui!)
    (swap! trays dissoc tray)
    (swap! actions #(apply dissoc % (:actions data)))
    (when (q/alive? tray) (q/tray-destroy tray)))
  nil)

(defn dispatch! [handle event value]
  (case event
    "tray-triggered" (when-let [data (get @actions handle)]
                       (when (:checkable data)
                         (swap! actions assoc-in [handle :checked]
                                (q/read-property handle "checked")))
                       (when-let [callback (:on-trigger data)] (callback)))
    "tray-activated" (when-let [callback (:on-activate (get @trays handle))]
                       (callback ({1 :context 2 :double-click 3 :trigger 4 :middle-click}
                                  (parse-long value) :unknown)))
    "tray-message-clicked" (when-let [callback (:on-message-click (get @trays handle))]
                             (callback))
    nil))

(defn clear!
  "Called by the application runner before Qt session disposal."
  []
  (let [failure (atom nil)]
    (doseq [tray (keys @trays)]
      (try (destroy! tray)
           (catch Throwable e (when-not @failure (reset! failure e)))))
    (reset! trays {})
    (reset! actions {})
    (when-let [error @failure] (throw error)))
  nil)
