(ns glimmer-qt.object
  "Typed QObject methods and GUI-thread subscriptions owned by live handles.
  Signal callbacks receive positional values; property observers receive one
  current value. Delivery is queued, never inline in a native setter or method."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.value :as value]))

(defonce ^:private connections (atom {}))
(defonce ^:private slots (atom {}))

(defn invoke!
  "Invoke an explicit public Qt method signature, e.g. setDuration(qlonglong).
  Arguments are a vector of typed values; void returns nil. Requires GUI thread."
  [target signature args]
  (q/invoke! target signature args))

(defn- handler! [handler]
  (when-not (ifn? handler) (throw (ex-info "Qt subscription requires a callable handler." {}))))

(defn- connect! [owner target name property? handler]
  (handler! handler)
  (let [token (q/subscribe owner target name property?)]
    (swap! connections assoc token {:owner owner :handler handler})
    token))

(defn subscribe!
  "Connect an exact signal signature. Owner/target destruction disconnects it.
  Native arguments use the value API. Safe worker-originated signals are copied
  and delivered on the GUI thread; this registration itself requires that thread."
  [owner target signature handler]
  (connect! owner target signature false handler))

(defn observe!
  "Observe a readable direct property with NOTIFY. Receives its current value
  on each notification. Read the initial value with ffi/read-property if needed.
  Notifications queued in a burst may all read the final property value."
  [owner target property handler]
  (connect! owner target property true handler))

(defn update-handler!
  "Refresh a live subscription's handler without reconnecting. Returns whether
  it is live. Events already queued use this latest handler. Requires GUI thread."
  [token handler]
  (handler! handler)
  (when-not (= 1 (q/gui-thread)) (throw (ex-info "Qt operation requires GUI thread." {})))
  (if (contains? @connections token)
    (do (swap! connections assoc-in [token :handler] handler) true)
    false))

(defn- forget! [token]
  (swap! connections dissoc token)
  (swap! slots (fn [owners]
                (into {} (keep (fn [[owner entries]]
                                 (let [entries (into {} (remove (fn [[_ v]] (= token (:token v))) entries))]
                                   (when (seq entries) [owner entries]))) owners)))))

(defn disconnect!
  "Idempotently disconnect and release the closure. Queued deliveries are inert.
  Requires GUI thread; a callback can disconnect itself."
  [token]
  (q/disconnect token)
  (forget! token)
  nil)

(defn disconnect-owner! [owner]
  (doseq [[token connection] @connections :when (= owner (:owner connection))]
    (disconnect! token)))

(defn validate-subscriptions! [specs]
  (when-not (or (nil? specs) (map? specs))
    (throw (ex-info ":qt/subscriptions must be a map." {})))
  (doseq [[_ {:keys [target signal property handler] :as spec}] specs]
    (when-not (and (map? spec) (every? #{:target :signal :property :handler} (keys spec))
                   (or (nil? target) (and (integer? target) (pos? target)))
                   (not= (nil? signal) (nil? property))
                   (string? (or signal property)) (seq (or signal property)))
      (throw (ex-info "Invalid Qt subscription descriptor." {:subscription spec})))
    (handler! handler)))

(defn reconcile! [owner specs]
  (validate-subscriptions! specs)
  (let [desired (into {} (map (fn [[key spec]] [key (assoc spec :target (or (:target spec) owner))]) specs))]
    (doseq [[key previous] (get @slots owner)]
      (when (not= (:source previous) (dissoc (get desired key) :handler))
        (disconnect! (:token previous))))
    (doseq [[key {:keys [target signal property handler] :as spec}] desired]
      (if-let [token (get-in @slots [owner key :token])]
        (update-handler! token handler)
        (let [token (connect! owner target (or signal property) (some? property) handler)]
          (swap! slots assoc-in [owner key] {:token token :source (dissoc spec :handler)})))))
  nil)

(defn dispatch! [token event payload]
  (case event
    "subscription-ended" (forget! token)
    "signal-error" (when (contains? @connections token)
                     (throw (ex-info payload {:subscription token})))
    "signal" (when-let [handler (get-in @connections [token :handler])]
               (apply handler (value/decode payload)))))

(defn live-count [] (count @connections))
(defn clear! [] (reset! connections {}) (reset! slots {}) nil)
