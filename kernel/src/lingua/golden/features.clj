;;; Clojure: nil, true and false, lists as seqs, loop and recur,
;;; and the printing functions, as Stonewyrm writes them
(def limit 12)
(def digits-of '(3 1 4 1 5 9 2 6))

(defn kind [x]
  (cond (zero? x) 'zero
        (odd? x) 'odd-digit
        (< x 10) 'even-digit
        :else 'big))

(defn kinds [l]
  (if (empty? l)
    '()
    (cons (kind (first l)) (kinds (rest l)))))

(defn digits [n]
  (loop [n n, acc nil]
    (if (< n 10)
      (cons n acc)
      (recur (quot n 10) (cons (rem n 10) acc)))))

(defn fib [n]
  (loop [i 0, a 0, b 1]
    (if (= i n) a (recur (inc i) b (+ a b)))))

(defn index-of [x l]
  (loop [l l, i 0]
    (cond (empty? l) nil
          (= x (first l)) i
          :else (recur (rest l) (inc i)))))

(defn squares [n]
  (loop [i n, acc '()]
    (if (zero? i) acc (recur (dec i) (conj acc (* i i))))))

(println (kinds '(0 1 2 12 7)))
(println (digits (fib 60)) (count (digits (fib 80))))
(prn "fib 80 =" (fib 80))
(println (index-of 5 digits-of) (index-of 7 digits-of))
(println (squares 6) (reverse (squares 3)))
(println (first nil) (rest nil) (next '(1)) (second digits-of) (conj nil 'a))
(println (and 1 nil) (or false nil) (or nil 'x) (not 0) (nil? (next '())))
(println (= (list 1 2) '(1 2)) (not= 'a 'b) (== 4 4) (< 1 2 3 3))
(println (symbol? 'abc) (symbol? nil) (seq? '()) (seq? nil) (some? false))
(dotimes [i 4]
  (print (* i limit) ""))
(newline)
(let [a (quot -7 2), b (rem -7 2), c (mod -7 2)]
  (println a b c (mod 7 -2) (abs -9) (max 1 9 3) (min 4 -2)))
(when-not (empty? digits-of)
  (print "not empty, ")
  (pr 'done)
  (newline))
(println (if-not nil 'yes 'no) (when false 1) (cond false 1) (do))
