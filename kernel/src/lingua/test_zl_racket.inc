/* test_zl_racket.inc — Racket (Dialectwyrm) front-end tests, included by
 * test_zl.c after test_zl_lisp.inc (whose helpers it uses). Expected
 * outputs are what racket (v8.10 [cs]) prints for the same programs: a
 * module prints each top-level value that is not void, in print style. */

#define RKT ZL_LANG_RACKET
#define RK  "#lang racket\n"
#define RB  "#lang racket/base\n"

static void test_racket_cases(void)
{
    static const lcase_t lc[] = {
        {RK "1 -42 0", "1\n-42\n0\n"},
        {RK "(displayln 5) (display 6) (newline) (write 7) (newline)", "5\n6\n7\n"},
        {RK "'foo 'Bar 'baz-quux", "'foo\n'Bar\n'baz-quux\n"},
        {RK "'() empty null '(1 2 3) '(a (b c) () (d . e) (1 2 . 3))",
         "'()\n'()\n'()\n'(1 2 3)\n'(a (b c) () (d . e) (1 2 . 3))\n"},
        {RK "#t #f #true #false (list #t #f)", "#t\n#f\n#t\n#f\n'(#t #f)\n"},
        {RK "(display (quote (a #t))) (newline) (write (quote x)) (print (quote x)) (newline)",
         "(a #t)\nx'x\n"},
        {RK "(displayln \"str\") (write \"a\\\"b\\\\c\") (display \"a\\\"b\\\\c\\n\")",
         "str\n\"a\\\"b\\\\c\"a\"b\\c\n"},
        {RK "(print \"q\") (println \"r\") (println '(1 a)) (print 5) (print '()) (newline)",
         "\"q\"\"r\"\n'(1 a)\n5'()\n"},
        {RK "4611686018427387903 -4611686018427387903 +7",
         "4611686018427387903\n-4611686018427387903\n7\n"},
        {RK "; only a comment\n", ""},
        {RK "", ""},
        {RK "(+) (+ 5) (+ 1 2 3 4) (- 5) (- 10 1 2 3) (*) (* 7) (* 2 3 4)",
         "0\n5\n10\n-5\n4\n1\n7\n24\n"},
        {RK "(/ 12 4) (/ 1) (/ -1) (/ 100 5 2)", "3\n1\n-1\n10\n"},
        {RK
         "(list (quotient 7 2) (quotient -7 2) (quotient 7 -2) (remainder -7 2) (remainder 7 -2))",
         "'(3 -3 -3 -1 1)\n"},
        {RK "(list (modulo 7 2) (modulo -7 2) (modulo 7 -2) (modulo -7 -2))", "'(1 1 -1 -1)\n"},
        {RK "(list (abs -3) (min 4) (max 4) (min 3 -1 2) (max 3 -1 9) (add1 5) (sub1 5))",
         "'(3 4 4 -1 9 6 4)\n"},
        {RK "(list (expt 2 10) (expt -2 3) (expt 5 0) (expt 0 0) (expt 1 -3) (expt -1 -3))",
         "'(1024 -8 1 1 1 -1)\n"},
        {RK "(list (= 1 1) (= 1 2) (= 2 2 2) (< 1 2 3) (< 1 3 2) (> 3 2 1) (<= 1 1 2) (>= 1 2))",
         "'(#t #f #t #t #f #t #t #f)\n"},
        {RK "(list (zero? 0) (zero? 3) (positive? 1) (negative? -1) (even? -4) (odd? -3))",
         "'(#t #f #t #t #t #t)\n"},
        {RK "(list (number? 1) (number? 'a) (integer? -5) (symbol? 'a) (symbol? '()) (symbol? 1))",
         "'(#t #f #t #t #f #f)\n"},
        {RK "(list (null? '()) (null? 0) (not #f) (not 0) (not '()) (pair? '(1)) (pair? '()))",
         "'(#t #f #t #f #f #t #f)\n"},
        {RK "(list (empty? '()) (empty? '(1)) (cons? '(1)) (cons? 'a))", "'(#t #f #t #f)\n"},
        {RK "(list (eq? 'a 'a) (eq? 'a 'b) (eqv? 7 7) (eq? '(1) '(1)) (equal? '(1 (2)) '(1 (2))))",
         "'(#t #f #t #f #t)\n"},
        {RK
         "(list (equal? 1 1) (equal? 'a 'b) (equal? '(1 2) '(1 2 3)) (equal? '(1 . 2) '(1 . 2)))",
         "'(#t #f #f #t)\n"},
        {RK "(list (car '(1 2)) (cdr '(1 2)) (cadr '(1 2)) (cddr '(1 2 3)) (caar '((1) 2)) (cdar "
            "'((1 5))))",
         "'(1 (2) 2 (3) 1 (5))\n"},
        {RK "(list (caddr '(1 2 3)) (cadddr '(1 2 3 4)) (cddddr '(1 2 3 4 5)))", "'(3 4 (5))\n"},
        {RK
         "(list (first '(1 2 3)) (rest '(1 2 3)) (second '(1 2 3)) (third '(1 2 3)) (rest '(1)))",
         "'(1 (2 3) 2 3 ())\n"},
        {RK "(cons 1 2) (cons 1 '()) (cons '() '())", "'(1 . 2)\n'(1)\n'(())\n"},
        {RK "(list) (list 1) (list 1 (list 2 3) 'x)", "'()\n'(1)\n'(1 (2 3) x)\n"},
        {RK "(list (length '()) (length '(1 2 3)) (length '((1 2) 3)))", "'(0 3 2)\n"},
        {RK "(append) (append '(1)) (append '(1 2) '(3)) (append '() '(3)) (append '(1) '(2) '(3 "
            "4) '() '(5)) (append '(1) 2)",
         "'()\n'(1)\n'(1 2 3)\n'(3)\n'(1 2 3 4 5)\n'(1 . 2)\n"},
        {RK "(reverse '()) (reverse '(1 (2 3) 4))", "'()\n'(4 (2 3) 1)\n"},
        {RK "(member 2 '(1 2 3)) (member 9 '(1 2)) (member '(1) '((1) 2)) (memq 'b '(a b)) (memv 3 "
            "'(3))",
         "'(2 3)\n#f\n'((1) 2)\n'(b)\n'(3)\n"},
        {RK "(assoc 'b '((a . 1) (b . 2) (b . 3))) (assq 'c '((a . 1))) (assv 2 '((1 . a) (2 . "
            "b))) (assoc '(k) '(((k) . 5)))",
         "'(b . 2)\n#f\n'(2 . b)\n'((k) . 5)\n"},
        {RK "(printf \"~a-~s-~v~n\" 1 'a '(b)) (printf \"~A ~S ~V ~~ ~%\" \"x\" \"y\" \"z\") "
            "(printf \"plain\\n\")",
         "1-a-'(b)\nx \"y\" \"z\" ~ \nplain\n"},
        {RK "(printf \"~a\" '(1 #t ())) (newline)", "(1 #t ())\n"},
        {RK "(if #t 1 2) (if #f 1 2) (if 0 'zero 'no) (if '() 'nil-is-true 'no)",
         "1\n2\n'zero\n'nil-is-true\n"},
        {RK "(if (> 2 1) (displayln 'a) (displayln 'b))", "a\n"},
        {RK "(cond [(= 1 2) 'a] [(= 1 1) 'b] [else 'c]) (cond [#f 1]) (cond [7])", "'b\n7\n"},
        {RK "(cond [(< 2 1) 'x] [else 'y]) (cond [#f] [(+ 1 1)])", "'y\n2\n"},
        {RK "(cond ((= 1 1) (display 'a) (display 'b)) (else (display 'c))) (newline)", "ab\n"},
        {RK "(when #t 1 2) (when #f 1) (unless #f 3) (unless #t 3)", "2\n3\n"},
        {RK "(when (= 1 1) (displayln 'w)) (unless (= 1 1) (displayln 'u))", "w\n"},
        {RK "(and) (and 1) (and 1 2) (and #f 2) (and 1 #f)", "#t\n1\n2\n#f\n#f\n"},
        {RK "(or) (or 1) (or #f 2) (or #f #f) (or 3 (car 5))", "#f\n1\n2\n#f\n3\n"},
        {RK "(if (and 1 2) 'y 'n) (if (or #f #f) 'y 'n) (if (and) 'y 'n)", "'y\n'n\n'y\n"},
        {RK "(let ([x 1] [y 2]) (+ x y)) (let () 5) (let ((x 1)) (let ([x 2] [y x]) (list x y)))",
         "3\n5\n'(2 1)\n"},
        {RK "(let ([x 1]) (let* ([x 2] [y x]) (list x y)))", "'(2 2)\n"},
        {RK "(define x 5) (set! x (+ x 1)) x (let ([y 1]) (set! y 7) y)", "6\n7\n"},
        {RK "(define a 1) (define b 2) (set! a 3) (set! b (+ a 1)) (list a b)", "'(3 4)\n"},
        {RK "(do ([i 0 (+ i 1)]) ((= i 3)) (displayln i)) (do ([i 0 (+ i 1)]) ((= i 0)) (displayln "
            "'never))",
         "0\n1\n2\n"},
        {RK "(do ([i 0 (+ i 1)] [acc '() (cons i acc)]) ((= i 3) acc))", "'(2 1 0)\n"},
        {RK "(do ([i 0 (+ i 1)] [j 10]) ((= i 2) (list i j)) (set! j (- j 1)))", "'(2 8)\n"},
        {RK "(do ([a 1 b] [b 2 a] [k 0 (+ k 1)]) ((= k 3) (list a b)))", "'(2 1)\n"},
        {RK "(let loop ([i 0]) (when (< i 3) (display i) (loop (+ i 1)))) (newline)", "012\n"},
        {RK "(let loop ([i 3] [acc '()]) (if (= i 0) acc (loop (- i 1) (cons i acc))))",
         "'(1 2 3)\n"},
        {RK "(define (f) (begin 1 2)) (f)", "2\n"},
        {RK "(void)", ""},
        {RK "(define (sq x) (* x x)) (sq 12) (define (f) 9) (f)", "144\n9\n"},
        {RK "(define (fact n) (if (< n 2) 1 (* n (fact (- n 1))))) (fact 20)",
         "2432902008176640000\n"},
        {RK "(define (count n acc) (if (= n 0) acc (count (- n 1) (+ acc 1)))) (count 100000 0)",
         "100000\n"},
        {RK "(define (ev? n) (if (= n 0) #t (od? (- n 1)))) (define (od? n) (if (= n 0) #f (ev? (- "
            "n 1)))) (ev? 1000)",
         "#t\n"},
        {RK "(define (f x) (define y (* x 2)) (define (g z) (+ y z)) (g 1)) (f 5)", "11\n"},
        {RK "(define g (lambda (x) (+ x 1))) (g 4)", "5\n"},
        {RK "(define (walk l) (cond [(null? l) 0] [(pair? (car l)) (+ (walk (car l)) (walk (cdr "
            "l)))] [else (+ (car l) (walk (cdr l)))])) (walk '(1 (2 3) (4 (5)) 6))",
         "21\n"},
        {RK "(define (show x) (display x) (newline)) (show 1) (show '(a b))", "1\n(a b)\n"},
        {RK "(define (maybe n) (when (> n 0) n)) (maybe 3) (maybe 0)", "3\n"},
        {RK "(define (pick n) (cond [(> n 0) 'pos] [(< n 0) 'neg])) (pick 1) (pick 0) (pick -1)",
         "'pos\n'neg\n"},
        {RK "(define (count-down n) (unless (= n 0) (display n) (count-down (- n 1)))) (count-down "
            "3) (newline)",
         "321\n"},
        {RK "(define (outer n) (let loop ([i 0] [s 0]) (if (> i n) s (loop (+ i 1) (+ s i))))) "
            "(outer 100)",
         "5050\n"},
        {RK "(define (f a b) (list b a)) (f 1 2) (define first-thing 4) first-thing",
         "'(2 1)\n4\n"},
        {RK "(define (add1x x) (add1 x)) (add1x 1)", "2\n"},
        {RK "(define (first l) 'mine) (first '(1 2))", "'mine\n"},
        {RK "(define lst '(3 1 2)) (define (ins x l) (cond [(null? l) (list x)] [(<= x (car l)) "
            "(cons x l)] [else (cons (car l) (ins x (cdr l)))])) (define (isort l) (if (null? l) "
            "'() (ins (car l) (isort (cdr l))))) (isort lst)",
         "'(1 2 3)\n"},
    };
    lisp_cases(RKT, lc, sizeof lc / sizeof lc[0]);
    /* #lang racket/base has no racket/list: first, empty and friends are unbound there */
    expect_out(RKT, RB "(add1 1) (printf \"~a~n\" (car '(1 2))) (println 'x)", "2\n1\n'x\n",
               "racket/base");
    expect_out(RKT, RB "(define (first l) (car l)) (define empty 0) (first '(7)) empty", "7\n0\n",
               "racket/base leaves first and empty to the program");
    expect_out(RKT, RK "(define (f empty) (list empty null)) (f 1)", "'(1 ())\n",
               "a parameter named empty shadows racket/list's");
    expect_out(RKT, "#lang racket/base  \n(display 1)", "1", "spaces after the #lang line");
}

static void test_racket_refusals(void)
{
    static const lref_t rf[] = {
        {"", ZL_E_SYNTAX, 1},
        {"(display 1)", ZL_E_SYNTAX, 1},
        {"#lang scheme\n(display 1)", ZL_E_UNSUPPORTED, 1},
        {"#lang racket/gui\n", ZL_E_UNSUPPORTED, 1},
        {"#lang racketx\n", ZL_E_UNSUPPORTED, 1},
        {"#lang racket/base2\n", ZL_E_UNSUPPORTED, 1},
        {RB "(first '(1))", ZL_E_UNDEFINED, 2},
        {RB "(empty? '())", ZL_E_UNDEFINED, 2},
        {RB "empty", ZL_E_UNDEFINED, 2},
        {RK "(display 1.5)", ZL_E_UNSUPPORTED, 2},
        {RK "(display 1/2)", ZL_E_UNSUPPORTED, 2},
        {RK "(display 4611686018427387904)", ZL_E_UNSUPPORTED, 2},
        {RK "(define (f) (lambda (x) x))", ZL_E_UNSUPPORTED, 2},
        {RK "(map add1 '(1 2))", ZL_E_UNSUPPORTED, 2},
        {RK "(for-each displayln '(1))", ZL_E_UNSUPPORTED, 2},
        {RK "(foldl + 0 '(1 2))", ZL_E_UNSUPPORTED, 2},
        {RK "(define (f x) x)\n(display f)", ZL_E_UNSUPPORTED, 3},
        {RK "add1", ZL_E_UNSUPPORTED, 2},
        {RK "(if 1 2)", ZL_E_SYNTAX, 2},
        {RK "(if 1 2 3 4)", ZL_E_SYNTAX, 2},
        {RK "(set-car! '(1) 2)", ZL_E_UNSUPPORTED, 2},
        {RK "(set-cdr! '(1) 2)", ZL_E_UNSUPPORTED, 2},
        {RK "(mcons 1 2)", ZL_E_UNSUPPORTED, 2},
        {RK "(floor-quotient 7 2)", ZL_E_UNDEFINED, 2},
        {RK "(floor 7)", ZL_E_UNSUPPORTED, 2},
        {RK "(exact->inexact 1)", ZL_E_UNSUPPORTED, 2},
        {RK "(begin (display 1))", ZL_E_UNSUPPORTED, 2},
        {RK "(define x (displayln 1))", ZL_E_UNSUPPORTED, 2},
        {RK "(define (f) (displayln 1))\n(+ (f) 1)", ZL_E_UNSUPPORTED, 3},
        {RK "(define (f n) (when (> n 0) n))\n(list (f 1))", ZL_E_UNSUPPORTED, 3},
        {RK "(list (void))", ZL_E_UNSUPPORTED, 2},
        {RK "(display \"a\" 1)", ZL_E_SYNTAX, 2},
        {RK "(printf \"~d\" 1)", ZL_E_UNSUPPORTED, 2},
        {RK "(printf \"~a ~a\" 1)", ZL_E_UNSUPPORTED, 2},
        {RK "(printf \"~a\" 1 2)", ZL_E_UNSUPPORTED, 2},
        {RK "(printf x)", ZL_E_UNSUPPORTED, 2},
        {RK "(let ([x 1)) x)", ZL_E_SYNTAX, 2},
        {RK "(let ((x 1]) x)", ZL_E_SYNTAX, 2},
        {RK "(display 1]", ZL_E_SYNTAX, 2},
        {RK "]", ZL_E_SYNTAX, 2},
        {RK "(let ([x 1]) x", ZL_E_SYNTAX, 2},
        {RK "{+ 1 2}", ZL_E_UNSUPPORTED, 2},
        {RK "(display #\\a)", ZL_E_UNSUPPORTED, 2},
        {RK "(display #(1 2))", ZL_E_UNSUPPORTED, 2},
        {RK "#;(display 1)", ZL_E_UNSUPPORTED, 2},
        {RK "#| c |#", ZL_E_UNSUPPORTED, 2},
        {RK "(case 1 [(1) 2])", ZL_E_UNSUPPORTED, 2},
        {RK "(for ([i 3]) (display i))", ZL_E_UNSUPPORTED, 2},
        {RK "(for/list ([i 3]) i)", ZL_E_UNSUPPORTED, 2},
        {RK "(match 1 [1 2])", ZL_E_UNSUPPORTED, 2},
        {RK "(struct p (x))", ZL_E_UNSUPPORTED, 2},
        {RK "(define-values (a b) (values 1 2))", ZL_E_UNSUPPORTED, 2},
        {RK "(string-append \"a\" \"b\")", ZL_E_UNSUPPORTED, 2},
        {RK "(format \"~a\" 1)", ZL_E_UNSUPPORTED, 2},
        {RK "(define s \"str\")", ZL_E_UNSUPPORTED, 2},
        {RK "(hash 1 2)", ZL_E_UNSUPPORTED, 2},
        {RK "`(1 ,x)", ZL_E_UNSUPPORTED, 2},
        {RK "(display \"a\nb\")", ZL_E_UNSUPPORTED, 2},
        {RK "(display \"\\t\")", ZL_E_UNSUPPORTED, 2},
        {RK "(display x)", ZL_E_UNDEFINED, 2},
        {RK "(f 1)\n(define (f x) x)", ZL_E_UNDEFINED, 2},
        {RK "(define (g) y)\n(g)\n(define y 1)", ZL_E_UNDEFINED, 3},
        {RK "(define (f x) x)\n(f 1 2)", ZL_E_SYNTAX, 3},
        {RK "(define x 1)\n(define x 2)", ZL_E_SYNTAX, 3},
        {RK "(define (f . r) r)", ZL_E_UNSUPPORTED, 2},
        {RK "(cond [else 1] [#t 2])", ZL_E_SYNTAX, 2},
        {RK "(cond [1 => add1])", ZL_E_UNSUPPORTED, 2},
        {RK "()", ZL_E_SYNTAX, 2},
        {RK "(define (f) (display 1) (define y 2) y)", ZL_E_SYNTAX, 2},
        {RK "(define (define) 1)", ZL_E_UNSUPPORTED, 2},
        {RK "(let ([if 1]) if)", ZL_E_UNSUPPORTED, 2},
        {RK "(call/cc (lambda (k) 1))", ZL_E_UNSUPPORTED, 2},
        {RK "(error 'x \"y\")", ZL_E_UNSUPPORTED, 2},
        {RK "(letrec ([f 1]) f)", ZL_E_UNSUPPORTED, 2},
    };
    lisp_refusals(RKT, rf, sizeof rf / sizeof rf[0]);
}

static void test_racket_traps(void)
{
    static const lref_t tr[] = {
        {RK "(display (/ 7 2))", ZL_E_INEXACT, 2}, /* racket goes on with 7/2 */
        {RK "(display (expt 2 -1))", ZL_E_INEXACT, 2},
        {RK "(display (quotient 1 0))", ZL_E_DIVZERO, 2},
        {RK "(display (modulo 1 0))", ZL_E_DIVZERO, 2},
        {RK "(display (car '()))", ZL_E_DOMAIN, 2},
        {RK "(display (cdr 5))", ZL_E_DOMAIN, 2},
        {RK "(display (first '()))", ZL_E_DOMAIN, 2},
        {RK "(display (first '(1 . 2)))", ZL_E_DOMAIN, 2},
        {RK "(display (rest '()))", ZL_E_DOMAIN, 2},
        {RK "(display (second '(1)))", ZL_E_DOMAIN, 2},
        {RK "(display (+ 'a 1))", ZL_E_DOMAIN, 2},
        {RK "(display (< 1 'b))", ZL_E_DOMAIN, 2},
        {RK "(display (= 'a 'a))", ZL_E_DOMAIN, 2},
        {RK "(display (zero? 'a))", ZL_E_DOMAIN, 2},
        {RK "(display (even? '()))", ZL_E_DOMAIN, 2},
        {RK "(display (length '(1 . 2)))", ZL_E_DOMAIN, 2},
        {RK "(display (abs '(1)))", ZL_E_DOMAIN, 2},
        {RK "(* 4611686018427387903 2)", ZL_E_OVERFLOW, 2}, /* racket has bignums */
        {RK "(add1 4611686018427387903)", ZL_E_OVERFLOW, 2},
        {RK "(define (f n) (if (= n 0) 0 (+ 1 (f (- n 1)))))\n(f 100000)", ZL_E_STACK, 2},
        {RK "(let loop () (loop))", ZL_E_FUEL, 2},
    };
    lisp_traps(RKT, tr, sizeof tr / sizeof tr[0]);
}

static void test_racket_twins(void)
{
    expect_twin(RKT, RK "(display   1)", RK "(display 1)\n");
    expect_twin(RKT, RB "(display 1)", RB "(display 1)\n");
    expect_twin(RKT, RK "; c\n(let ([x 1] [y 2]) [+ x y])",
                RK "; c\n(let ((x 1) (y 2))\n  (+ x y))\n");
    expect_twin(RKT, RK "(list empty null '()) (empty? empty) (cons? null)",
                RK "(list '() '() '())\n(null? '())\n(pair? '())\n");
    expect_twin(RKT, RK "(define f (lambda (x) (* x x))) (f 2)",
                RK "(define (f x)\n  (* x x))\n(f 2)\n");
    expect_twin(RKT, RK "(cond [(= 1 2) 'a] [else 'b]) #true #false",
                RK "(cond\n  ((= 1 2) 'a)\n  (else 'b))\n#t\n#f\n");
    expect_twin(RKT, RK "(first (rest '(1 2))) (add1 (sub1 0)) (printf \"~a~n\" 1)",
                RK "(first (rest '(1 2)))\n(add1 (sub1 0))\n(printf \"~a~n\" 1)\n");
    zl_diag_t d;
    CHECK(compile(RKT | ZL_TWIN, RK "(display 1)\n", &d) == ZL_OK, "canonical Racket accepted");
    CHECK(compile(RKT | ZL_TWIN, RK "(let ([x 1])\n  x)\n", &d) == ZL_E_NONCANON && d.line == 2,
          "brackets are not canonical");
    CHECK(compile(RKT | ZL_TWIN, RK "(display empty)\n", &d) == ZL_E_NONCANON,
          "empty is written '()");
    CHECK(compile(RKT | ZL_TWIN, RK "(empty? '())\n", &d) == ZL_E_NONCANON,
          "empty? is written null?");
    CHECK(compile(RKT | ZL_TWIN, "#lang racket \n(display 1)\n", &d) == ZL_E_NONCANON,
          "a space after the #lang line");
    CHECK(compile(RKT | ZL_TWIN, RK "\n(display 1)\n", &d) == ZL_E_NONCANON,
          "a blank line after the #lang line");
    /* both spellings give the same IR */
    static zl_prog_t a;
    CHECK(compile(RKT, RK "(let ([x 1]) (list x empty))", &d) == ZL_OK, "brackets compile");
    memcpy(&a, &P, sizeof a);
    CHECK(compile(RKT, RK "(let ((x 1)) (list x '()))", &d) == ZL_OK && zl_prog_equal(&a, &P),
          "brackets and parentheses, empty and '() give the same IR");
}

/* (read): the next integer of the input (racket reads stdin); none left
 * traps INPUT where racket reads eof. A bare (read) is printed. */
static void test_racket_input(void)
{
    const int64_t in[] = {4, 1, 2}, in2[] = {-8, 5};
    expect_in(RKT, RB "(+ (read) 1)\n", in, 1, "5\n", "read at the top level");
    expect_in(RKT, RK "(displayln (list (read) (read)))\n", in + 1, 2, "(1 2)\n",
              "arguments read in order");
    expect_in(RKT, RB "(read)\n(define x (read))\n(printf \"~a~n\" (* x 3))\n", in2, 2, "-8\n15\n",
              "a bare read prints its value");
    expect_twin(RKT, RB "(displayln [read])\n", RB "(displayln (read))\n");
    expect_trap(RKT, RB "(read)\n", ZL_E_INPUT, 2, "read with no input");
    int s = run_src(RKT, RB "(read)\n(read)\n", in, 1, 0);
    CHECK(s == ZL_E_INPUT && R.trap_line == 3 && strcmp(out, "4\n") == 0,
          "read past the last input: %d [%s]", s, out);
    static const lref_t rf[] = {
        {RB "(read (current-input-port))", ZL_E_UNSUPPORTED, 2},
        {RB "(displayln (read-line))", ZL_E_UNSUPPORTED, 2},
        {RB "(displayln (read-char))", ZL_E_UNSUPPORTED, 2},
    };
    lisp_refusals(RKT, rf, sizeof rf / sizeof rf[0]);
}

static void test_racket(void)
{
    test_racket_cases();
    test_racket_refusals();
    test_racket_traps();
    test_racket_twins();
    test_racket_input();
}
