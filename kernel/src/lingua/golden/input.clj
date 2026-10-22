;; Input: a count, that many integers and then two more, one per line.
;; Prints the integers' sum, largest and list, then the gcd of the two.
(defn read-ints [n]
  (if (zero? n) () (let [x (read)] (cons x (read-ints (dec n))))))

(defn total [l]
  (if (empty? l) 0 (+ (first l) (total (rest l)))))

(defn largest [l]
  (if (empty? (rest l)) (first l) (max (first l) (largest (rest l)))))

(defn gcd2 [a b]
  (if (zero? b) (abs a) (recur b (mod a b))))

(def xs (read-ints (read)))
(println "sum" (total xs))
(println "largest" (largest xs))
(prn xs)
(println "gcd" (gcd2 (read) (read)))
