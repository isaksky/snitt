(ns glimmer-qt.ffi
  "Checked C ABI. Handles are opaque, non-recycled uint64 IDs, never addresses.
  Scheduling operations are worker-safe; widget operations require the main thread. Invalid/disposed handles
  throw managed errors; alive? is the explicit non-throwing lifetime probe."
  (:require [jolt.ffi :as ffi]
            [glimmer-qt.value :as value]
            [clojure.string :as str]))

(ffi/defcfn native-error "gqt_error" [] :string)

(defmacro defchecked [sym native types result-type & options]
  (let [raw (symbol (str "native-" (name sym)))
        args (vec (repeatedly (count types) #(gensym "arg")))
        result (gensym "result")]
    `(do
       (ffi/defcfn ~raw ~native ~types ~result-type ~@options)
       (defn ~sym ~args
         (let [~result (~raw ~@args)]
           (when ~(if (= result-type :string) `(nil? ~result) `(zero? ~result))
             (throw (ex-info (native-error) {:operation ~(name sym)})))
           ~result)))))

(defchecked open "gqt_open" [:string :int :int :string] :uint64)
(defchecked open-application "gqt_open_application" [:string] :uint64)
(defchecked window-create "gqt_window_create" [:uint64 :string :int :int] :uint64)
(defchecked window-container "gqt_window_container" [:uint64] :uint64)
(defchecked window-action "gqt_window_action" [:uint64 :int] :int)
(defchecked window-destroy "gqt_window_destroy" [:uint64] :int)
(defchecked screens-native "gqt_screens" [:uint64] :string)
(defn screens [session] (value/decode (screens-native session)))
(defchecked window-place "gqt_window_place" [:uint64 :uint64 :int :int :int :int] :int)
(defchecked window-style "gqt_window_style" [:uint64 :int :double :int] :int)
(defchecked window-with-native "gqt_window_with_native" [:uint64 :pointer :pointer] :int)
(defchecked container "gqt_container" [:uint64] :uint64)
(defchecked set-handler "gqt_set_handler" [:uint64 :pointer] :int)
(defchecked clipboard-text "gqt_clipboard_text" [:uint64] :string)
(defchecked clipboard-image "gqt_clipboard_image" [:uint64] :uint64)
(defchecked image-load "gqt_image_load" [:uint64 :string] :uint64)
(defchecked image-save "gqt_image_save" [:uint64 :uint64 :string] :int)
(ffi/defcfn image-equal-native "gqt_image_equal" [:uint64 :uint64 :uint64] :int)
(defn image-equal [session left right]
  (let [result (image-equal-native session left right)]
    (when (neg? result) (throw (ex-info (native-error) {:operation "image-equal"})))
    (= 1 result)))
(defchecked image-dispose "gqt_image_dispose" [:uint64 :uint64] :int)
(defchecked clipboard-publish-text "gqt_clipboard_publish_text" [:uint64 :string] :int)
(defchecked clipboard-publish-image "gqt_clipboard_publish_image" [:uint64 :uint64] :int)
(defchecked open-url "gqt_open_url" [:uint64 :string] :int)
(defchecked reveal-file "gqt_reveal_file" [:uint64 :string] :int)
(ffi/defcfn tray-available "gqt_tray_available" [] :int)
(defchecked tray-create "gqt_tray_create" [:uint64 :string :string] :uint64)
(defchecked tray-show "gqt_tray_show" [:uint64 :int] :int)
(defchecked tray-set-icon "gqt_tray_set_icon" [:uint64 :string] :int)
(defchecked tray-set-tooltip "gqt_tray_set_tooltip" [:uint64 :string] :int)
(defchecked tray-add-action "gqt_tray_add_action" [:uint64 :string :string :int :int :int :int] :uint64)
(defchecked tray-update-action "gqt_tray_update_action" [:uint64 :string :string :int :int :int] :int)
(defchecked tray-notify "gqt_tray_notify" [:uint64 :string :string :int :int] :int)
(defchecked tray-destroy "gqt_tray_destroy" [:uint64] :int)
(defchecked create-registered "gqt_create_registered" [:uint64 :string :string :string] :uint64)
(defchecked child-target "gqt_child_target" [:uint64 :string] :uint64)
(defchecked bind-first-frame "gqt_media_bind_first_frame" [:uint64] :int)
(defchecked write-value "gqt_write_value" [:uint64 :string :string] :int)
(defchecked read-value "gqt_read_value" [:uint64 :string] :string)
(defchecked reset-native "gqt_reset_property" [:uint64 :string] :int)
(defchecked local-path-native "gqt_local_path" [:string] :string)
(defchecked invoke-native "gqt_invoke" [:uint64 :pointer :pointer] :string :blocking)
(defchecked subscribe-native "gqt_subscribe" [:uint64 :uint64 :string :int] :uint64)
(defchecked disconnect-native "gqt_disconnect" [:uint64] :int)
(defchecked write-controlled "gqt_write_controlled" [:uint64 :string :string] :int)
(ffi/defcfn subscription-count "gqt_subscription_count" [] :int)
(defn- property-args! [handle property]
  (when-not (and (integer? handle) (<= 1 handle 18446744073709551615N))
    (throw (ex-info "Invalid Qt object handle." {:handle handle})))
  (when-not (and (string? property) (not (empty? property))
                 (not (str/includes? property "\u0000")))
    (throw (ex-info "Invalid Qt property name." {:property property}))))
(defn set-property! [handle property v]
  (property-args! handle property)
  (write-value handle property (value/encode v)))
(defn read-property [handle property]
  (property-args! handle property)
  (value/decode (read-value handle property)))
(defn set-controlled-property! [handle property v]
  (property-args! handle property)
  (write-controlled handle property (value/encode v)))
(defn invoke! [handle signature args]
  (property-args! handle signature)
  (when-not (vector? args) (throw (ex-info "Qt method arguments must be a vector." {})))
  ;; Blocking calls must not retain movable managed string storage during GC.
  (ffi/with-c-string [method-name signature]
    (ffi/with-c-string [arguments (value/encode args)]
      (value/decode (invoke-native handle method-name arguments)))))
(defn subscribe [owner target name property?]
  (property-args! owner name)
  (property-args! target name)
  (subscribe-native owner target name (if property? 1 0)))
(defn disconnect [token]
  (property-args! token "subscription")
  (disconnect-native token))
(defn reset-property!
  "With no default, invokes Qt RESET or throws if unsupported. With a default,
  performs a checked write. Does not reconstruct arbitrary removed QML bindings."
  ([handle property] (property-args! handle property) (reset-native handle property))
  ([handle property default] (set-property! handle property default)))
(defn local-file-path [url] (value/decode (local-path-native (value/encode url))))
;; Compatibility setters use the same checked, lossless marshaller.
(defn set-string [handle property v] (set-property! handle property v))
(defn set-number [handle property v] (set-property! handle property v))
(defn set-bool [handle property v] (set-property! handle property (not (zero? v))))
(defchecked append "gqt_append" [:uint64 :uint64] :int)
(ffi/defcfn is-item "gqt_is_item" [:uint64] :int)
(defchecked reorder "gqt_reorder" [:uint64 :uint64 :uint64] :int)
(defchecked remove-widget "gqt_remove" [:uint64] :int)
(defchecked owner "gqt_owner" [:uint64] :uint64)
(defchecked destroy-owner "gqt_destroy_owner" [:uint64] :int)
(ffi/defcfn schedule "gqt_schedule" [:uint64 :uint64 :uint64 :int :int] :int)
(ffi/defcfn cancel "gqt_cancel" [:uint64 :uint64] :int)
(ffi/defcfn restart "gqt_restart" [:uint64 :uint64 :int] :int)
(ffi/defcfn pending-count "gqt_pending_count" [] :int)
(ffi/defcfn timer-count "gqt_timer_count" [] :int)
(ffi/defcfn gui-thread "gqt_gui_thread" [] :int)
(ffi/defcfn post "gqt_post" [:uint64 :uint64 :int] :int)
;; Qt owns the main thread, but Jolt workers and GC must remain able to run.
(ffi/defcfn exec "gqt_exec" [:uint64] :int :blocking)
(defchecked exit "gqt_exit" [:uint64 :int] :int)
(defchecked close "gqt_close" [:uint64] :int)
(ffi/defcfn alive "gqt_alive" [:uint64] :int)
(defn alive? [handle] (and (integer? handle) (pos? handle) (= 1 (alive handle))))
(ffi/defcfn live-count "gqt_live_count" [] :int)
(ffi/defcfn owned-count "gqt_owned_count" [] :int)
(defchecked emit-signal "gqt_emit" [:uint64 :string] :int)
(defchecked window-snapshot "gqt_window_snapshot" [:uint64 :string] :int)
(defchecked save-window "gqt_save_window" [:uint64 :string] :int)
(defchecked request-close "gqt_request_close" [:uint64] :int)
(defchecked destroy-native "gqt_destroy_native" [:uint64] :int)

(defn create
  "Compatibility constructor for built-ins; application code uses widget/create!."
  [session tag]
  (when-not (#{"vbox" "hbox" "label" "entry" "button"} tag)
    (throw (ex-info "Unsupported Qt component tag" {:tag tag})))
  (create-registered session (str "qrc:/glimmer-qt/" tag ".qml")
                     (value/encode {}) (if (#{"entry" "button"} tag) "relay" "")))

(defchecked measure-text-native "gqt_measure_text" [:uint64 :string] :string)
(defn measure-text
  "Measure a single line with a live control's font on the GUI thread. Returns
  string keys: advance, height, ascent, descent, underline-position, line-width,
  x, y, width in logical pixels. No widget mutation or temporary native owners."
  [handle text]
  (property-args! handle "font")
  (when-not (and (string? text) (not (str/includes? text "\u0000")))
    (throw (ex-info "Text measurement requires a string without NUL." {})))
  (value/decode (measure-text-native handle text)))

(defchecked pointer-accept "gqt_pointer_accept" [:uint64 :int] :int)
(defchecked map-point-native "gqt_map_point" [:uint64 :uint64 :double :double] :string)
(defn map-point
  "Map local [x y] from a live attached item to another item, or nil for global
  desktop coordinates. Preserves fractional pixels, transforms and negative origins."
  [from to x y]
  (value/decode (map-point-native from (or to 0) (double x) (double y))))

(defchecked configure-keys "gqt_keys" [:uint64 :int :int] :int)
(defchecked focus-native "gqt_focus" [:uint64 :int] :int)
