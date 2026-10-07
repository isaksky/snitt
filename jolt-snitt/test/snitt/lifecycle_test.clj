(ns snitt.lifecycle-test
  (:require [clojure.test :refer [deftest is run-tests]]
            [snitt.lifecycle :as lifecycle]))

(deftest argument-policy
  (is (= ["/work/image one.png" "--show" "--background"]
         (lifecycle/normalize-arguments #(str "/work/" %) ["image one.png" "--show" "--background"])))
  (is (= [:capture] (lifecycle/command [])))
  (is (= [:image "/work/a.png"] (lifecycle/command ["/work/a.png"])))
  (is (= [:show] (lifecycle/command ["/work/a.png" "--show"])))
  (is (= [:background] (lifecycle/command ["/work/a.png" "--background"])))
  (is (= [:quit] (lifecycle/command ["/work/a.png" "--quit"])))
  (is (= [:image "/work/a.png"] (lifecycle/startup-command ["/work/a.png" "--show"])))
  (is (= [:show] (lifecycle/startup-command ["--show" "--background"])))
  (is (= [:background] (lifecycle/startup-command ["--background"]))))

(defn -main [& _]
  (let [result (run-tests 'snitt.lifecycle-test)]
    (when (pos? (+ (:fail result) (:error result)))
      (throw (ex-info "Lifecycle tests failed" result)))))
