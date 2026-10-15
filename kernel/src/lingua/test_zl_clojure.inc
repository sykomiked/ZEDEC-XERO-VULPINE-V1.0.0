/* test_zl_clojure.inc — Clojure (Stonewyrm) front-end tests, included by
 * test_zl.c after test_zl_lisp.inc (whose helpers it uses). Expected
 * outputs are what clojure (1.11.1, run as "clojure file.clj") prints for
 * the same programs. */

#define CLJL ZL_LANG_CLOJURE

static void test_clojure_cases(void)
{
    static const lcase_t lc[] = {
        /* basics */
        {"(println 1 -42 0 +7)", "1 -42 0 7\n"},
        {"(println 4611686018427387903 -4611686018427387903)",
         "4611686018427387903 -4611686018427387903\n"},
        {"(println 'foo 'Bar 'baz-quux 'a? 'b! 'x_y 'a1)", "foo Bar baz-quux a? b! x_y a1\n"},
        {"(println nil true false '() () '(1 2 3) '(a (b c) () nil true))",
         "nil true false () () (1 2 3) (a (b c) () nil true)\n"},
        {"(print 1) (print 2) (newline) (print) (println) (println \"a\" \"b\")", "12\n\na b\n"},
        {"(prn \"q\" 'r 1 nil '(1 a)) (pr \"s\\\"t\\\\u\") (pr) (prn)",
         "\"q\" r 1 nil (1 a)\n\"s\\\"t\\\\u\"\n"},
        {"(println \"tab-free\\nline\") (print \"no newline at the end\")",
         "tab-free\nline\nno newline at the end"},
        {"; only a comment\n", ""},
        {"", ""},
        {";; a comment\n(println 1) ; trailing\n(println 2)", "1\n2\n"},
        {"(println 1, 2,, 3) (println '(1, 2))", "1 2 3\n(1 2)\n"},
        {"(println (+) (+ 5) (+ 1 2 3 4) (- 5) (- 10 1 2 3) (*) (* 7) (* 2 3 4))",
         "0 5 10 -5 4 1 7 24\n"},
        {"(println (/ 12 4) (/ 1) (/ -1) (/ 100 5 2) (/ -12 4))", "3 1 -1 10 -3\n"},
        {"(println (quot 7 2) (quot -7 2) (quot 7 -2) (rem -7 2) (rem 7 -2) (rem 7 2))",
         "3 -3 -3 -1 1 1\n"},
        {"(println (mod 7 2) (mod -7 2) (mod 7 -2) (mod -7 -2) (mod 0 3))", "1 1 -1 -1 0\n"},
        {"(println (abs -3) (abs 3) (min 4) (max 4) (min 3 -1 2) (max 3 -1 9) (inc 5) (dec 5))",
         "3 3 4 4 -1 9 6 4\n"},
        {"(println (= 1 1) (= 1 2) (= 2 2 2) (= 2 2 3) (< 1 2 3) (< 1 3 2) (> 3 2 1) (<= 1 1 2) "
         "(>= 1 2))",
         "true false true false true false true true false\n"},
        {"(println (= 1) (not= 1) (< 1) (> 1) (<= 1) (>= 1) (== 1) (== 2 2) (== 2 3) (not= 1 2) "
         "(not= 1 1 1))",
         "true false true true true true true true false true false\n"},
        {"(println (= 'a 'a) (= 'a 'b) (= '(1 (2)) (list 1 (list 2))) (= nil '()) (= nil nil) (= "
         "'() (list)))",
         "true false true false true true\n"},
        {"(println (= 1 'a) (= '(1 2) '(1 2 3)) (= true true) (= false nil))",
         "false false true false\n"},
        {"(println (zero? 0) (zero? 3) (pos? 1) (pos? 0) (neg? -1) (neg? 0) (even? -4) (odd? -3) "
         "(even? 0))",
         "true false true false true false true true true\n"},
        {"(println (number? 1) (number? 'a) (integer? -5) (int? 3) (number? nil) (integer? '()))",
         "true false true true false false\n"},
        {"(println (symbol? 'a) (symbol? nil) (symbol? true) (symbol? false) (symbol? 1) (symbol? "
         "'(a)))",
         "true false false false false false\n"},
        {"(println (nil? nil) (nil? false) (nil? '()) (some? nil) (some? false) (some? 0))",
         "true false false false true true\n"},
        {"(println (true? true) (true? 1) (false? false) (false? nil) (not nil) (not false) (not "
         "0) (not '()))",
         "true false true false true true false false\n"},
        {"(println (seq? '()) (seq? '(1)) (seq? nil) (seq? 1) (empty? nil) (empty? '()) (empty? "
         "'(1)))",
         "true true false false true true false\n"},
        {"(println (first '(1 2 3)) (rest '(1 2 3)) (next '(1 2 3)) (second '(1 2 3)))",
         "1 (2 3) (2 3) 2\n"},
        {"(println (first '()) (first nil) (rest '()) (rest nil) (next '()) (next nil) (next '(1)) "
         "(rest '(1)))",
         "nil nil () () nil nil nil ()\n"},
        {"(println (second '(1)) (second nil) (first '(nil 2)) (second '(1 nil)))",
         "nil nil nil nil\n"},
        {"(println (cons 1 '(2)) (cons 1 nil) (cons 1 '()) (cons '() '()) (cons nil nil))",
         "(1 2) (1) (1) (()) (nil)\n"},
        {"(println (list) (list 1) (list 1 (list 2 3) 'x) (list nil true))",
         "() (1) (1 (2 3) x) (nil true)\n"},
        {"(println (count '()) (count nil) (count '(1 2 3)) (count '((1 2) 3)))", "0 0 3 2\n"},
        {"(println (conj '(1 2) 3) (conj '(1) 2 3 4) (conj nil 1) (conj '() 1) (conj nil nil) "
         "(conj 5))",
         "(3 1 2) (4 3 2 1) (1) (1) (nil) 5\n"},
        {"(println (reverse '()) (reverse nil) (reverse '(1 (2 3) 4)))", "() () (4 (2 3) 1)\n"},
        {"(println (min 'a) (max nil) (min '(1)))", "a nil (1)\n"},
        /* forms */
        {"(println (if true 1 2) (if false 1 2) (if nil 1 2) (if 0 'zero 'no) (if '() 'empty 'no))",
         "1 2 2 zero empty\n"},
        {"(println (if false 1) (if true 1) (if-not false 1 2) (if-not 0 1 2) (if-not nil 3))",
         "nil 1 1 2 3\n"},
        {"(if (> 2 1) (println 'a) (println 'b))", "a\n"},
        {"(println (cond (= 1 2) 'a (= 1 1) 'b :else 'c) (cond false 1) (cond nil 1 :else 2) "
         "(cond))",
         "b nil 2 nil\n"},
        {"(println (cond (< 2 1) 'x true 'y))", "y\n"},
        {"(println (when true 1 2) (when false 1) (when-not false 3) (when-not true 3) (when 1))",
         "2 nil 3 nil nil\n"},
        {"(when (= 1 1) (println 'w)) (when-not (= 1 1) (println 'u))", "w\n"},
        {"(println (and) (and 1) (and 1 2) (and false 2) (and 1 nil) (and nil false))",
         "true 1 2 false nil nil\n"},
        {"(println (or) (or 1) (or false 2) (or false nil) (or nil false) (or 3 (first 5)))",
         "nil 1 2 nil false 3\n"},
        {"(println (and 1 (println 'side) 2)) (println (or (println 'once) 4))",
         "side\nnil\nonce\n4\n"},
        {"(if (and 1 nil) (println 'y) (println 'n)) (if (or nil 0) (println 'y) (println 'n))",
         "n\ny\n"},
        {"(println (do) (do 1) (do (print 'a) 2))", "anil 1 2\n"},
        {"(println (let [x 1] x) (let [x 1 y 2] (+ x y)) (let [x 1 x (+ x 1)] x) (let []))",
         "1 3 2 nil\n"},
        {"(println (let [x 1] (let [y (+ x 1)] (let [x 10] (+ x y)))))", "12\n"},
        {"(println (let [a 1 b (inc a) c (* b 3)] (list a b c)))", "(1 2 6)\n"},
        {"(println (loop [i 0 acc 1] (if (= i 5) acc (recur (inc i) (* acc 2)))))", "32\n"},
        {"(println (loop [i 3 l nil] (if (zero? i) l (recur (dec i) (cons i l)))))", "(1 2 3)\n"},
        {"(println (loop [] 7) (loop [x 1] (when (< x 100) (recur (* x 3)))))", "7 nil\n"},
        {"(println (loop [a 1 b (inc a)] (if (> a 3) (list a b) (recur (inc a) (+ a b)))))",
         "(4 8)\n"},
        {"(println (loop [i 0] (cond (= i 3) 'three :else (recur (inc i)))))", "three\n"},
        {"(println (loop [i 0] (if (and (< i 9) (not= i 4)) (recur (inc i)) i)))", "4\n"},
        {"(println (loop [i 0] (let [j (inc i)] (if (< j 5) (recur j) j))))", "5\n"},
        {"(println (let [n 4] (loop [i 0 s 0] (if (> i n) s (recur (inc i) (+ s i))))))", "10\n"},
        {"(println (loop [i 0 out '()] (if (= i 2) out (recur (inc i) (loop [j 0 o out] (if (= j "
         "2) o (recur (inc j) (conj o (list i j)))))))))",
         "((1 1) (1 0) (0 1) (0 0))\n"},
        {"(dotimes [i 3] (print i \"\")) (newline) (dotimes [i 0] (print 'never)) (println "
         "(dotimes [i 1] i))",
         "0 1 2 \nnil\n"},
        {"(dotimes [i 2] (dotimes [j 2] (print (list i j)))) (newline)", "(0 0)(0 1)(1 0)(1 1)\n"},
        /* definitions */
        {"(defn sq [x] (* x x)) (println (sq 5) (sq -3))", "25 9\n"},
        {"(defn f [] 42) (defn g [] (println 'g)) (defn h []) (println (f) (g) (h))",
         "g\n42 nil nil\n"},
        {"(defn add3 [a b c] (+ a b c)) (println (add3 1 2 3))", "6\n"},
        {"(defn fact [n] (if (zero? n) 1 (* n (fact (dec n))))) (println (fact 20))",
         "2432902008176640000\n"},
        {"(defn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (println (fib 20))",
         "6765\n"},
        {"(defn sum-to [n acc] (if (zero? n) acc (recur (dec n) (+ acc n)))) (println (sum-to "
         "100000 0))",
         "5000050000\n"},
        {"(defn len [l] (loop [l l n 0] (if (empty? l) n (recur (rest l) (inc n))))) (println (len "
         "'(a b c)))",
         "3\n"},
        {"(defn rev [l] (loop [l l out '()] (if (seq? l) (if (empty? l) out (recur (rest l) (cons "
         "(first l) out))) out))) (println (rev '(1 2 3)))",
         "(3 2 1)\n"},
        {"(defn countdown [n] (when (pos? n) (print n \"\") (recur (dec n)))) (countdown 4) "
         "(println)",
         "4 3 2 1 \n"},
        {"(defn twice [x] (println 'x x) (* 2 x)) (println (twice (twice 1)))", "x 1\nx 2\n4\n"},
        {"(def x 5) (def y (* x 2)) (println x y)", "5 10\n"},
        {"(def l '(1 2 3)) (defn total [l] (if (empty? l) 0 (+ (first l) (total (rest l))))) "
         "(println (total l))",
         "6\n"},
        {"(def n 3) (defn addn [x] (+ x n)) (println (addn 4))", "7\n"},
        {"(defn f [first] (inc first)) (println (f 1) (first '(9)))", "2 9\n"},
        {"(defn f [x] (let [list 5] (+ x list))) (println (f 1) (list 1))", "6 (1)\n"},
        {"(defn collatz [n] (cond (= n 1) 0 (even? n) (inc (collatz (quot n 2))) :else (inc "
         "(collatz (inc (* 3 n)))))) (println (collatz 27))",
         "111\n"},
        {"(defn member? [x l] (cond (empty? l) false (= x (first l)) true :else (recur x (rest "
         "l)))) (println (member? 2 '(1 2)) (member? 5 '(1 2)))",
         "true false\n"},
        {"(defn even2? [n] (if (zero? n) true (not (even2? (dec n))))) (println (even2? 10) "
         "(even2? 7))",
         "true false\n"},
        {"(defn f [x] (if (> x 0) (do (print x) (f (dec x))) (println))) (f 3)", "321\n"},
    };
    lisp_cases(CLJL, lc, sizeof lc / sizeof lc[0]);
}

static void test_clojure_refusals(void)
{
    static const lref_t rf[] = {
        {"(println 1.5)", ZL_E_UNSUPPORTED, 1},
        {"(println 1/2)", ZL_E_UNSUPPORTED, 1},
        {"(println 1N)", ZL_E_UNSUPPORTED, 1},
        {"(println 0x10)", ZL_E_UNSUPPORTED, 1},
        {"(println 017)", ZL_E_UNSUPPORTED, 1},
        {"(println 4611686018427387904)", ZL_E_UNSUPPORTED, 1},
        {"(println 9223372036854775807)", ZL_E_UNSUPPORTED, 1},
        {"(println \"a\" 'b) (str \"a\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(def s \"str\")", ZL_E_UNSUPPORTED, 1},
        {"(println (count \"abc\"))", ZL_E_UNSUPPORTED, 1},
        {"(println [1 2])", ZL_E_UNSUPPORTED, 1},
        {"(println '[1 2])", ZL_E_UNSUPPORTED, 1},
        {"(println {:a 1})", ZL_E_UNSUPPORTED, 1},
        {"(println #{1})", ZL_E_UNSUPPORTED, 1},
        {"(println :a)", ZL_E_UNSUPPORTED, 1},
        {"(println :else)", ZL_E_UNSUPPORTED, 1},
        {"(:else 1)", ZL_E_UNSUPPORTED, 1},
        {"(println \\a)", ZL_E_UNSUPPORTED, 1},
        {"(println @x)", ZL_E_UNSUPPORTED, 1},
        {"(println `(1 ~x))", ZL_E_UNSUPPORTED, 1},
        {"(println ^:m x)", ZL_E_UNSUPPORTED, 1},
        {"(println #(inc %))", ZL_E_UNSUPPORTED, 1},
        {"#_(println 1)", ZL_E_UNSUPPORTED, 1},
        {"(println #\"re\")", ZL_E_UNSUPPORTED, 1},
        {"(println #'inc)", ZL_E_UNSUPPORTED, 1},
        {"(println (.toString 1))", ZL_E_UNSUPPORTED, 1},
        {"(println (Math/abs -1))", ZL_E_UNSUPPORTED, 1},
        {"(println java.lang.Long)", ZL_E_UNSUPPORTED, 1},
        {"(println x')", ZL_E_UNSUPPORTED, 1},
        {"(println (fn [x] x))", ZL_E_UNSUPPORTED, 1},
        {"(map inc '(1 2))", ZL_E_UNSUPPORTED, 1},
        {"(filter odd? '(1 2))", ZL_E_UNSUPPORTED, 1},
        {"(reduce + '(1 2))", ZL_E_UNSUPPORTED, 1},
        {"(apply + '(1 2))", ZL_E_UNSUPPORTED, 1},
        {"(defn f [x] x)\n(println f)", ZL_E_UNSUPPORTED, 2},
        {"(println inc)", ZL_E_UNSUPPORTED, 1},
        {"(let [f inc] (f 1))", ZL_E_UNSUPPORTED, 1},
        {"(defn f [x] x)\n(let [g f] (g 1))", ZL_E_UNSUPPORTED, 2},
        {"(defn f \"doc\" [x] x)", ZL_E_UNSUPPORTED, 1},
        {"(defn f ([x] x) ([x y] y))", ZL_E_UNSUPPORTED, 1},
        {"(defn f [x & r] x)", ZL_E_UNSUPPORTED, 1},
        {"(defn f [[a b]] a)", ZL_E_UNSUPPORTED, 1},
        {"(let [[a b] '(1 2)] a)", ZL_E_UNSUPPORTED, 1},
        {"(defn f [x x] x)", ZL_E_UNSUPPORTED, 1},
        {"(loop [a 1 a 2] a)", ZL_E_UNSUPPORTED, 1},
        {"(def x)", ZL_E_UNSUPPORTED, 1},
        {"(def x \"doc\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(def x 1)\n(def x 2)", ZL_E_UNSUPPORTED, 2},
        {"(defn f [] 1)\n(defn f [] 2)", ZL_E_UNSUPPORTED, 2},
        {"(def f 1)\n(defn f [] 2)", ZL_E_UNSUPPORTED, 2},
        {"(defn inc [x] x)", ZL_E_UNSUPPORTED, 1},
        {"(def list 1)", ZL_E_UNSUPPORTED, 1},
        {"(defn nil [] 1)", ZL_E_UNSUPPORTED, 1},
        {"(let [nil 1] 1)", ZL_E_SYNTAX, 1},
        {"(let [true 1] 1)", ZL_E_SYNTAX, 1},
        {"(let [:else 1] 1)", ZL_E_UNSUPPORTED, 1},
        {"(let [x 1 y] x)", ZL_E_SYNTAX, 1},
        {"(let (x 1) x)", ZL_E_SYNTAX, 1},
        {"(loop (x 1) x)", ZL_E_SYNTAX, 1},
        {"(dotimes (i 3) i)", ZL_E_SYNTAX, 1},
        {"(dotimes [i] i)", ZL_E_SYNTAX, 1},
        {"(dotimes [i 3 r] i)", ZL_E_SYNTAX, 1},
        {"(cond 1)", ZL_E_SYNTAX, 1},
        {"(cond :else 1 true 2)", ZL_E_UNSUPPORTED, 1},
        {"(if 1)", ZL_E_SYNTAX, 1},
        {"(if 1 2 3 4)", ZL_E_SYNTAX, 1},
        {"(recur 1)", ZL_E_SYNTAX, 1},
        {"(defn f [x] (+ 1 (recur x)))", ZL_E_SYNTAX, 1},
        {"(defn f [x] (recur x 1))", ZL_E_SYNTAX, 1},
        {"(loop [i 0] (println (recur 1)))", ZL_E_SYNTAX, 1},
        {"(dotimes [i 2] (recur 1))", ZL_E_SYNTAX, 1},
        {"(defn f [x] (if (recur x) 1 2))", ZL_E_SYNTAX, 1},
        {"(defn f [x] (and (recur x) 1))", ZL_E_SYNTAX, 1},
        {"(println x)", ZL_E_UNDEFINED, 1},
        {"(println (f 1))\n(defn f [x] x)", ZL_E_UNDEFINED, 1},
        {"(defn f [] (g))\n(defn g [] 1)", ZL_E_UNDEFINED, 1},
        {"(defn f [] y)\n(def y 1)", ZL_E_UNDEFINED, 1},
        {"(def x (+ x 1))", ZL_E_UNDEFINED, 1},
        {"(defn f [x] (defn g [] 1))", ZL_E_UNSUPPORTED, 1},
        {"(defn f [x] (def y 1))", ZL_E_UNSUPPORTED, 1},
        {"(atom 1)", ZL_E_UNSUPPORTED, 1},
        {"(def a (atom 0))\n(swap! a inc)", ZL_E_UNSUPPORTED, 1},
        {"(set! x 1)", ZL_E_UNSUPPORTED, 1},
        {"(try 1)", ZL_E_UNSUPPORTED, 1},
        {"(throw 1)", ZL_E_UNSUPPORTED, 1},
        {"(doseq [x '(1)] (println x))", ZL_E_UNSUPPORTED, 1},
        {"(for [x '(1)] x)", ZL_E_UNSUPPORTED, 1},
        {"(case 1 1 2)", ZL_E_UNSUPPORTED, 1},
        {"(while true 1)", ZL_E_UNSUPPORTED, 1},
        {"(range 3)", ZL_E_UNSUPPORTED, 1},
        {"(nth '(1) 0)", ZL_E_UNSUPPORTED, 1},
        {"(last '(1))", ZL_E_UNSUPPORTED, 1},
        {"(concat '(1) '(2))", ZL_E_UNSUPPORTED, 1},
        {"(vector 1)", ZL_E_UNSUPPORTED, 1},
        {"(str 1)", ZL_E_UNSUPPORTED, 1},
        {"(format \"%d\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(printf \"%d\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(defmacro m [] 1)", ZL_E_UNSUPPORTED, 1},
        {"(-> 1 inc)", ZL_E_UNSUPPORTED, 1},
        {"(ns foo)", ZL_E_UNSUPPORTED, 1},
        {"(comment 1)", ZL_E_UNSUPPORTED, 1},
        {"(println (+ 1 2)", ZL_E_SYNTAX, 1},
        {"(println 1))", ZL_E_SYNTAX, 1},
        {"]", ZL_E_SYNTAX, 1},
        {"(println 1]", ZL_E_SYNTAX, 1},
        {"[1 2)", ZL_E_SYNTAX, 1},
        {"}", ZL_E_SYNTAX, 1},
        {"(println '(1 . 2))", ZL_E_UNSUPPORTED, 1},
        {"(println (. 1 toString))", ZL_E_UNSUPPORTED, 1},
        {"(println car)", ZL_E_UNDEFINED, 1},
        {"(car '(1))", ZL_E_UNDEFINED, 1},
        {"(length '(1))", ZL_E_UNDEFINED, 1},
        {"(begin 1)", ZL_E_UNDEFINED, 1},
        {"(define x 1)", ZL_E_UNDEFINED, 1},
        {"(lambda [x] x)", ZL_E_UNDEFINED, 1},
        {"(println #t)", ZL_E_UNSUPPORTED, 1},
        {"(println \"a\\tb\")", ZL_E_UNSUPPORTED, 1},
        {"(println 'a.b)", ZL_E_UNSUPPORTED, 1},
        {"(println 'a/b)", ZL_E_UNSUPPORTED, 1},
        {"(println 'a:b)", ZL_E_UNSUPPORTED, 1},
        {"(println %)", ZL_E_UNSUPPORTED, 1},
        {"(println 'a$b)", ZL_E_UNSUPPORTED, 1},
        {"(1 2)", ZL_E_SYNTAX, 1},
        {"([1] 0)", ZL_E_UNSUPPORTED, 1},
        {"((fn [x] x) 1)", ZL_E_UNSUPPORTED, 1},
        {"(println '(1 #_2))", ZL_E_UNSUPPORTED, 1},
        {"(let* [x 1] x)", ZL_E_UNSUPPORTED, 1},
        {"(var inc)", ZL_E_UNSUPPORTED, 1},
        {"(new Object)", ZL_E_UNSUPPORTED, 1},
        {"(prn (quote))", ZL_E_SYNTAX, 1},
        {"(quote 1 2)", ZL_E_SYNTAX, 1},
        {"(defn f [] 1)\n(f 1)", ZL_E_SYNTAX, 2},
        {"(println (first))", ZL_E_SYNTAX, 1},
        {"(println (cons 1))", ZL_E_SYNTAX, 1},
        {"(println (conj))", ZL_E_SYNTAX, 1},
        {"(println (list? '()))", ZL_E_UNSUPPORTED, 1},
        {"(println (seq '(1)))", ZL_E_UNSUPPORTED, 1},
        {"(defn g [x] x)\n(defn f [] (let [g 1] (g 2)))", ZL_E_UNSUPPORTED, 2},
        {"(println \"a\nb\")", ZL_E_UNSUPPORTED, 1},
    };
    lisp_refusals(CLJL, rf, sizeof rf / sizeof rf[0]);
    /* the other dialects keep their own names */
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SCHEME, "(define x 1)", &d) == ZL_OK, "define is Scheme's");
    CHECK(compile(CLJL, "(define x 1)", &d) == ZL_E_UNDEFINED, "and not Clojure's");
    CHECK(compile(ZL_LANG_LISP, "(print (cadr '(1 2)))", &d) == ZL_OK, "cadr is LISP's");
    CHECK(compile(CLJL, "(println (cadr '(1 2)))", &d) == ZL_E_UNDEFINED, "and not Clojure's");
}

static void test_clojure_traps(void)
{
    static const lref_t tr[] = {
        {"(println (/ 7 2))", ZL_E_INEXACT, 1}, /* clojure goes on with the ratio 7/2 */
        {"(println (* 4611686018427387903 2))", ZL_E_OVERFLOW, 1}, /* a long reaches 2^63 */
        {"(println (quot 1 0))", ZL_E_DIVZERO, 1},
        {"(println (rem 1 0))", ZL_E_DIVZERO, 1},
        {"(println (mod 1 0))", ZL_E_DIVZERO, 1},
        {"(println (/ 1 0))", ZL_E_DIVZERO, 1},
        {"(println (first 5))", ZL_E_DOMAIN, 1},
        {"(println (rest 'a))", ZL_E_DOMAIN, 1},
        {"(println (next 1))", ZL_E_DOMAIN, 1},
        {"(println (cons 1 2))", ZL_E_DOMAIN, 1},
        {"(println (count 7))", ZL_E_DOMAIN, 1},
        {"(println (conj 1 2))", ZL_E_DOMAIN, 1},
        {"(println (reverse 3))", ZL_E_DOMAIN, 1},
        {"(println (empty? 3))", ZL_E_DOMAIN, 1},
        {"(println (second 3))", ZL_E_DOMAIN, 1},
        {"(println (+ 'a 1))", ZL_E_DOMAIN, 1},
        {"(println (+ nil 1))", ZL_E_DOMAIN, 1},
        {"(println (< 1 'b))", ZL_E_DOMAIN, 1},
        {"(println (== 'a 'a))", ZL_E_DOMAIN, 1},
        {"(println (zero? 'a))", ZL_E_DOMAIN, 1},
        {"(println (pos? nil))", ZL_E_DOMAIN, 1},
        {"(println (even? '()))", ZL_E_DOMAIN, 1},
        {"(println (inc true))", ZL_E_DOMAIN, 1},
        {"(println (dec 'a))", ZL_E_DOMAIN, 1},
        {"(println (abs '(1)))", ZL_E_DOMAIN, 1},
        {"(println (- false))", ZL_E_DOMAIN, 1},
        {"(println (* 4611686018427387903 4))", ZL_E_OVERFLOW, 1},
        {"(println 1)\n(println (inc 4611686018427387903))", ZL_E_OVERFLOW, 2}, /* 2^62 */
        {"(defn f [n] (if (= n 0) 0 (+ 1 (f (- n 1)))))\n(println (f 100000))", ZL_E_STACK, 0},
        {"(defn f [n] (f n))\n(f 1)", ZL_E_RSTACK, 0}, /* a named self call is a call */
        {"(loop [] (recur))", ZL_E_FUEL, 0},
    };
    lisp_traps(CLJL, tr, sizeof tr / sizeof tr[0]);
}

static void test_clojure_twins(void)
{
    expect_twin(CLJL, "(println   1)", "(println 1)\n");
    expect_twin(CLJL, ";; c\n(let [x 1, y 2] (+ x y)) ; t",
                "; c\n(let [x 1 y 2]\n  (+ x y))\n; t\n");
    expect_twin(CLJL, "(cond (= 1 2) 'a :else 'b)", "(cond\n  (= 1 2) 'a\n  :else 'b)\n");
    expect_twin(CLJL, "(defn f [x] (* x x)) (f 2)", "(defn f [x]\n  (* x x))\n(f 2)\n");
    expect_twin(CLJL, "(defn g [] (do))", "(defn g []\n  (do))\n");
    expect_twin(CLJL, "'nil '5 'true '() 'a '(1 nil)", "nil\n5\ntrue\n'()\n'a\n'(1 nil)\n");
    expect_twin(CLJL, "(loop [i 0] (when (< i 3) (recur (inc i))))",
                "(loop [i 0]\n  (when (< i 3)\n    (recur (inc i))))\n");
    expect_twin(CLJL, "(dotimes [i 2] (print i)) (if-not nil 1 2) (when-not nil 1)",
                "(dotimes [i 2]\n  (print i))\n(if-not nil 1 2)\n(when-not nil\n  1)\n");
    zl_diag_t d;
    CHECK(compile(CLJL | ZL_TWIN, "(println 1)\n", &d) == ZL_OK, "canonical Clojure accepted");
    CHECK(compile(CLJL | ZL_TWIN, "(let [x 1, y 2]\n  x)\n", &d) == ZL_E_NONCANON && d.line == 1,
          "commas are not canonical");
    CHECK(compile(CLJL | ZL_TWIN, ";; c\n(println 1)\n", &d) == ZL_E_NONCANON,
          "a comment is written with one semicolon");
    /* both spellings give the same IR */
    static zl_prog_t a;
    CHECK(compile(CLJL, "(let [x 1, y 2] (list x y 'nil))", &d) == ZL_OK, "commas compile");
    memcpy(&a, &P, sizeof a);
    CHECK(compile(CLJL, "(let [x 1 y 2] (list x y nil))", &d) == ZL_OK && zl_prog_equal(&a, &P),
          "commas are blanks, and 'nil is nil");
}

static void test_clojure(void)
{
    test_clojure_cases();
    test_clojure_refusals();
    test_clojure_traps();
    test_clojure_twins();
}
