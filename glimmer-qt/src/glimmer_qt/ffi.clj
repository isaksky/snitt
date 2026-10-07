(ns glimmer-qt.ffi
  "Checked C ABI. Handles are opaque, non-recycled uint64 IDs, never addresses.
  All operations except post require the main thread. Invalid/disposed handles
  throw managed errors; alive? is the explicit non-throwing lifetime probe."
  (:require [jolt.ffi :as ffi]))

(ffi/defcfn native-error "gqt_error" [] :string)

(defmacro defchecked [sym native types result-type]
  (let [raw (symbol (str "native-" (name sym)))
        args (vec (repeatedly (count types) #(gensym "arg")))
        result (gensym "result")]
    `(do
       (ffi/defcfn ~raw ~native ~types ~result-type)
       (defn ~sym ~args
         (let [~result (~raw ~@args)]
           (when ~(if (= result-type :string) `(nil? ~result) `(zero? ~result))
             (throw (ex-info (native-error) {:operation ~(name sym)})))
           ~result)))))

(defchecked open "gqt_open" [:string :int :int :string] :uint64)
(defchecked container "gqt_container" [:uint64] :uint64)
(defchecked set-handler "gqt_set_handler" [:uint64 :pointer] :int)
(defchecked create "gqt_create" [:uint64 :string] :uint64)
(defchecked set-string "gqt_set_string" [:uint64 :string :string] :int)
(defchecked set-number "gqt_set_number" [:uint64 :string :double] :int)
(defchecked set-bool "gqt_set_bool" [:uint64 :string :int] :int)
(defchecked append "gqt_append" [:uint64 :uint64] :int)
(defchecked reorder "gqt_reorder" [:uint64 :uint64 :uint64] :int)
(defchecked remove-widget "gqt_remove" [:uint64] :int)
(ffi/defcfn post "gqt_post" [:uint64 :uint64 :int] :int)
;; Qt owns the main thread, but Jolt workers and GC must remain able to run.
(ffi/defcfn exec "gqt_exec" [:uint64] :int :blocking)
(defchecked exit "gqt_exit" [:uint64 :int] :int)
(defchecked close "gqt_close" [:uint64] :int)
(ffi/defcfn alive "gqt_alive" [:uint64] :int)
(defn alive? [handle] (and (integer? handle) (pos? handle) (= 1 (alive handle))))
(ffi/defcfn live-count "gqt_live_count" [] :int)
(ffi/defcfn owned-count "gqt_owned_count" [] :int)
(defchecked read-property "gqt_read" [:uint64 :string] :string)
(defchecked emit-signal "gqt_emit" [:uint64 :string] :int)
(defchecked save-window "gqt_save_window" [:uint64 :string] :int)
(defchecked request-close "gqt_request_close" [:uint64] :int)
(defchecked destroy-native "gqt_destroy_native" [:uint64] :int)
