(ns glimmer-qt.services
  "GUI-thread clipboard and desktop operations. Each call returns {:ok? true
  :value ...} or {:ok? false :error ...}; failed publication never signals that
  an editor may discard its document. Images are session-owned native QImages:
  release-image! when finished, and never use one after the session closes."
  (:require [glimmer-qt.ffi :as q]
            [glimmer-qt.widget :as widget]
            [clojure.string :as str]))

(defrecord Image [session handle])

(defn- result [f]
  (try {:ok? true :value (f)}
       (catch Throwable e {:ok? false :error (ex-message e)})))

(defn- session! []
  (or @widget/session
      (throw (ex-info "No running Qt application." {}))))

(defn- c-string! [value label]
  (when-not (and (string? value) (not (str/includes? value "\u0000")))
    (throw (ex-info (str label " must be a string without NUL.") {})))
  value)

(defn- image-handle! [session image]
  (when-not (and (instance? Image image) (= session (:session image)))
    (throw (ex-info "Image belongs to another Qt session or is invalid." {})))
  (:handle image))

(defn read-text [] (result #(q/clipboard-text (session!))))
(defn publish-text! [text]
  (result #(do
             (q/clipboard-publish-text (session!) (c-string! text "Text"))
             true)))
(defn read-image []
  (result #(let [session (session!)]
             (->Image session (q/clipboard-image session)))))
(defn load-image [path]
  (result #(let [session (session!)]
             (->Image session (q/image-load session (c-string! path "Image path"))))))
(defn save-image! [image path]
  (result #(let [session (session!)]
             (q/image-save session (image-handle! session image) (c-string! path "Image path"))
             true)))
(defn same-image? [left right]
  (result #(let [session (session!)]
             (q/image-equal session (image-handle! session left)
                            (image-handle! session right)))))
(defn release-image! [image]
  (result #(let [session (session!)]
             (q/image-dispose session (image-handle! session image))
             true)))
(defn publish-image! [image]
  (result #(let [session (session!)]
             (q/clipboard-publish-image session (image-handle! session image))
             true)))
(defn open-url! [url]
  (result #(do
             (q/open-url (session!) (c-string! url "URL"))
             true)))
(defn reveal-file! [path]
  (result #(do
             (q/reveal-file (session!) (c-string! path "File path"))
             true)))
