;;; The same three algorithms as collatz.f, collatz.bas and collatz.fs
(defn steps [n]
  (loop [k n, s 0]
    (cond (= k 1) s
          (even? k) (recur (quot k 2) (inc s))
          :else (recur (+ (* 3 k) 1) (inc s)))))

(defn euclid [a b]
  (if (zero? b) a (recur b (mod a b))))

(defn table [n]
  (when (<= n 12)
    (println n (steps n))
    (recur (inc n))))

(table 1)
(println "GCD" (euclid 1071 462))

(loop [i 0, f 0, g 1]
  (when (< i 15)
    (println f)
    (recur (inc i) g (+ f g))))
