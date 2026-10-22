/* test_zl_commonlisp.inc — Common Lisp (Braidwyrm) front-end tests,
 * included by test_zl.c after test_zl_lisp.inc (whose helpers it uses).
 * Common Lisp is the LISP core of zl_lisp.c plus case; expected outputs
 * are what sbcl --script prints for the same programs. */

#define CLL ZL_LANG_COMMONLISP

static void test_commonlisp_cases(void)
{
    static const lcase_t lc[] = {
        {"(print 1) (print -42) (prin1 5) (princ 6) (terpri)", "\n1 \n-42 56\n"},
        {"(print 'foo) (print 'Bar) (print nil) (print t) (print ()) (print '(a (b c) () (d . e)))",
         "\nFOO \nBAR \nNIL \nT \nNIL \n(A (B C) NIL (D . E)) "},
        {"(print \"str\") (prin1 \"a\\\"b\") (princ \"c\")", "\n\"str\" \"a\\\"b\"c"},
        {"(print (list (+ 1 2 3) (- 10 1 2) (* 2 3 4) (/ 12 4) (1+ 5) (1- 5)))",
         "\n(6 7 24 3 6 4) "},
        {"(print (list (floor 7 2) (floor -7 2) (truncate -7 2) (ceiling 7 2) (mod -7 2) (rem -7 "
         "2)))",
         "\n(3 -4 -3 4 1 -1) "},
        {"(print (list (abs -3) (min 3 -1 2) (max 3 -1 9) (expt 2 10) (expt -2 3)))",
         "\n(3 -1 9 1024 -8) "},
        {"(print (list (= 1 1) (/= 1 2) (< 1 2 3) (> 3 1 2) (<= 1 1 2) (>= 2 2 1)))",
         "\n(T T T NIL T T) "},
        {"(print (list (zerop 0) (plusp 1) (minusp 0) (evenp -4) (oddp -3) (numberp 'a) (integerp "
         "5)))",
         "\n(T T NIL T T NIL T) "},
        {"(print (list (symbolp 'a) (symbolp nil) (null nil) (not 1) (consp '(1)) (atom 1) (listp "
         "nil)))",
         "\n(T T T NIL T T T) "},
        {"(print (list (eq 'a 'a) (eql 7 7) (equal '(1 (2)) '(1 (2))) (equal '(1) '(2))))",
         "\n(T T T NIL) "},
        {"(print (list (car '(1 2)) (cdr '(1 2)) (car nil) (cadr '(1 2)) (cddr '(1 2 3)) (caddr "
         "'(1 2 3))))",
         "\n(1 (2) NIL 2 (3) 3) "},
        {"(print (list (first '(1 2 3)) (rest '(1 2 3)) (second '(1 2 3)) (third '(1 2 3)) (first "
         "nil)))",
         "\n(1 (2 3) 2 3 NIL) "},
        {"(print (list (cons 1 2) (list) (length '(1 2 3)) (append '(1) '(2 3)) (reverse '(1 2 "
         "3))))",
         "\n((1 . 2) NIL 3 (1 2 3) (3 2 1)) "},
        {"(print (list (member 2 '(1 2 3)) (assoc 'b '((a . 1) (b . 2))) (assoc 'c '((a . 1)))))",
         "\n((2 3) (B . 2) NIL) "},
        {"(defvar *p* (list 1 2)) (print (rplaca *p* 9)) (print (rplacd *p* 7))",
         "\n(9 2) \n(9 . 7) "},
        {"(format t \"~a ~d ~s~%~~~%\" 'x 42 \"s\")", "X 42 \"s\"\n~\n"},
        {"(print (case 1 (1 'one) (2 'two))) (print (case 2 (1 'one) (2 'two))) (print (case 3 (1 "
         "'one)))",
         "\nONE \nTWO \nNIL "},
        {"(print (case 'b (a 1) ((b c) 2) (t 3))) (print (case 'z (a 1) (otherwise 3)))",
         "\n2 \n3 "},
        {"(print (case 5 ((1 2 3) 'low) ((4 5 6) 'mid) (otherwise 'high)))", "\nMID "},
        {"(print (case (+ 1 1) (2 (print 'side) 'two)))", "\nSIDE \nTWO "},
        {"(case 7 (7 (print 'seven) (print 'again)))", "\nSEVEN \nAGAIN "},
        {"(print (case 1 (1)))", "\nNIL "},
        {"(print (case 'x (otherwise)))", "\nNIL "},
        {"(print (case 3 ((t) 'tee) (3 'three)))", "\nTHREE "},
        {"(print (case t ((t) 'tee) (otherwise 'no)))", "\nTEE "},
        {"(defun f (x) (case x (0 'zero) (otherwise (f (- x 1))))) (print (f 5))", "\nZERO "},
        {"(defun g (x) (case (mod x 3) (0 'fizz) (1 nil) (2 'two))) (print (list (g 3) (g 4) (g "
         "5)))",
         "\n(FIZZ NIL TWO) "},
        {"(print (if t 1 2)) (print (if nil 1)) (print (cond ((= 1 2) 'a) (t 'b)))",
         "\n1 \nNIL \nB "},
        {"(print (when t 1 2)) (print (unless t 3)) (print (and 1 2)) (print (or nil 2))",
         "\n2 \nNIL \n2 \n2 "},
        {"(print (let ((x 1) (y 2)) (+ x y))) (print (let* ((x 1) (y (+ x 1))) (* x y)))",
         "\n3 \n2 "},
        {"(defvar *n* 5) (setq *n* 6) (setf *n* (+ *n* 1)) (print *n*) (incf *n*) (decf *n* 3) "
         "(print *n*)",
         "\n7 \n5 "},
        {"(defvar *s* nil) (push 1 *s*) (push 2 *s*) (print (pop *s*)) (print *s*)", "\n2 \n(1) "},
        {"(dotimes (i 3) (print i)) (print (do ((i 0 (1+ i)) (acc nil (cons i acc))) ((= i 3) "
         "acc)))",
         "\n0 \n1 \n2 \n(2 1 0) "},
        {"(print (labels ((f (n) (if (zerop n) 0 (+ n (f (1- n)))))) (f 10)))", "\n55 "},
        {"(print (progn 1 2 3))", "\n3 "},
        {"(defun sq (x) (* x x)) (print (sq 12))", "\n144 "},
        {"(defun fact (n) (if (< n 2) 1 (* n (fact (- n 1))))) (print (fact 20))",
         "\n2432902008176640000 "},
        {"(defun count-up (n acc) (if (= n 0) acc (count-up (- n 1) (+ acc 1)))) (print (count-up "
         "100000 0))",
         "\n100000 "},
        {"(defun ev (n) (if (zerop n) t (od (1- n)))) (defun od (n) (if (zerop n) nil (ev (1- "
         "n)))) (print (ev 100))",
         "\nT "},
        {"(defun walk (l) (cond ((null l) 0) ((consp (car l)) (+ (walk (car l)) (walk (cdr l)))) "
         "(t (+ (car l) (walk (cdr l)))))) (print (walk '(1 (2 3) (4 (5)) 6)))",
         "\n21 "},
    };
    lisp_cases(CLL, lc, sizeof lc / sizeof lc[0]);
}

static void test_commonlisp_refusals(void)
{
    static const lref_t rf[] = {
        {"(case)", ZL_E_SYNTAX, 1},
        {"(case 1 x)", ZL_E_SYNTAX, 1},
        {"(case 1 ())", ZL_E_SYNTAX, 1},
        {"(case 1 (otherwise 2) (1 3))", ZL_E_SYNTAX, 1},
        {"(case 1 (t 2) (1 3))", ZL_E_SYNTAX, 1},
        {"(case 1 (nil 2))", ZL_E_UNSUPPORTED, 1},
        {"(case 1 (() 2))", ZL_E_UNSUPPORTED, 1},
        {"(case 1 ((1 . 2) 2))", ZL_E_UNSUPPORTED, 1},
        {"(case 1 ((1 (2)) 2))", ZL_E_UNSUPPORTED, 1},
        {"(case 1 (\"a\" 2))", ZL_E_UNSUPPORTED, 1},
        {"(case 1 ('a 2))", ZL_E_UNSUPPORTED, 1},
        {"(ecase 1 (1 2))", ZL_E_UNDEFINED, 1},
        {"(defun f () (return-from f 1))", ZL_E_UNSUPPORTED, 1},
        {"(block b (return-from b 1))", ZL_E_UNSUPPORTED, 1},
        {"(loop for i from 1 to 3 do (print i))", ZL_E_UNSUPPORTED, 1},
        {"(mapcar #'1+ '(1 2))", ZL_E_UNSUPPORTED, 1},
        {"(funcall 'f 1)", ZL_E_UNSUPPORTED, 1},
        {"(print 1.5)", ZL_E_UNSUPPORTED, 1},
        {"(print 1/2)", ZL_E_UNSUPPORTED, 1},
        {"(print :key)", ZL_E_UNSUPPORTED, 1},
        {"(defun f (&optional x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defvar *x* 1) (let ((*x* 2)) *x*)", ZL_E_UNSUPPORTED, 1},
        {"(setf (car x) 1)", ZL_E_UNSUPPORTED, 1},
        {"(defmacro m () 1)", ZL_E_UNSUPPORTED, 1},
        {"(values 1 2)", ZL_E_UNSUPPORTED, 1},
        {"(format nil \"~a\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(format t \"~5d\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(defun car (x) x)", ZL_E_UNSUPPORTED, 1},
        {"(print x)", ZL_E_UNDEFINED, 1},
        {"(f)\n(defun f () 1)", ZL_E_UNDEFINED, 1},
        {"(print (aref a 0))", ZL_E_UNSUPPORTED, 1},
    };
    lisp_refusals(CLL, rf, sizeof rf / sizeof rf[0]);
    /* LISP keeps its older subset: case is Common Lisp's here */
    zl_diag_t d;
    CHECK(compile(ZL_LANG_LISP, "(print (case 1 (1 2)))", &d) == ZL_E_UNSUPPORTED,
          "case is refused in LISP");
    CHECK(compile(CLL, "(print (case 1 (1 2)))", &d) == ZL_OK, "and admitted in Common Lisp");
}

static void test_commonlisp_traps(void)
{
    static const lref_t tr[] = {
        {"(print (/ 7 2))", ZL_E_INEXACT, 1}, /* sbcl goes on with 7/2 */
        {"(print (floor 1 0))", ZL_E_DIVZERO, 1},
        {"(print (car 5))", ZL_E_DOMAIN, 1},
        {"(print (first 5))", ZL_E_DOMAIN, 1},
        {"(print (case (car 5) (1 2)))", ZL_E_DOMAIN, 1},
        {"(print (+ 'a 1))", ZL_E_DOMAIN, 1},
        {"(print (* 4611686018427387903 2))", ZL_E_OVERFLOW, 1}, /* sbcl has bignums */
        {"(defun f (n) (case n (0 0) (t (+ 1 (f (- n 1))))))\n(print (f 100000))", ZL_E_STACK, 1},
    };
    lisp_traps(CLL, tr, sizeof tr / sizeof tr[0]);
}

static void test_commonlisp_twins(void)
{
    expect_twin(CLL, "(PRINT   1)", "(print 1)\n");
    expect_twin(CLL, "(DEFUN SQ (X) (* X X)) (Print (Sq 3))",
                "(defun sq (x)\n  (* x x))\n(print (sq 3))\n");
    expect_twin(CLL,
                "(defvar x '(1 2 3)) (print (car '(A b))) (print (cdr x)) (print (cadr x)) (print "
                "(caddr x))",
                "(defvar x '(1 2 3))\n(print (first '(a b)))\n(print (rest x))\n(print (second "
                "x))\n(print (third "
                "x))\n");
    expect_twin(CLL, "(defvar x '(1 2 3)) (print (first x)) (print (cddr x)) (print (caadr x))",
                "(defvar x '(1 2 3))\n(print (first x))\n(print (cddr x))\n(print (caadr x))\n");
    expect_twin(CLL, "(defvar x 0) (defvar y 0) (setq x 1 y 2) (setf x 3)",
                "(defvar x 0)\n(defvar y 0)\n(setf x 1 y 2)\n(setf x 3)\n");
    expect_twin(CLL, "(let (a (b)) (print NIL) (print ()) (print 'T))",
                "(let ((a nil) (b nil))\n  (print nil)\n  (print nil)\n  (print t))\n");
    expect_twin(CLL, "(defvar x 2) (case x (1 'a) ((2 3) 'b) (t 'c))",
                "(defvar x 2)\n(case x\n  (1 'a)\n  ((2 3) 'b)\n  (otherwise 'c))\n");
    expect_twin(CLL, "(format t \"~A~%\" \"Mixed Case\") ; Comment Kept",
                "(format t \"~A~%\" \"Mixed Case\")\n; Comment Kept\n");
    /* 'T is written T, so a cond clause headed 'T is the else clause in both */
    expect_twin(CLL, "(print (cond (nil 1) ('t 2)))",
                "(print (cond\n         (nil 1)\n         (t 2)))\n");
    expect_twin(ZL_LANG_LISP, "(print (cond ('t)))", "(PRINT (COND\n         (T)))\n");
    zl_diag_t d;
    CHECK(compile(CLL | ZL_TWIN, "(print 1)\n", &d) == ZL_OK, "canonical Common Lisp accepted");
    CHECK(compile(CLL | ZL_TWIN, "(PRINT 1)\n", &d) == ZL_E_NONCANON && d.line == 1,
          "upper case is not canonical");
    CHECK(compile(CLL | ZL_TWIN, "(defvar x nil)\n(print (car x))\n", &d) == ZL_E_NONCANON,
          "car is written first");
    CHECK(compile(CLL | ZL_TWIN, "(defvar x nil)\n(setq x 1)\n", &d) == ZL_E_NONCANON,
          "setq is written setf");
    CHECK(compile(CLL | ZL_TWIN, "(case 1\n  (t 2))\n", &d) == ZL_E_NONCANON,
          "t is written otherwise");
    /* the same program in LISP and Common Lisp prints the same */
    const char *src = "(defun f (n) (if (zerop n) nil (cons n (f (1- n))))) (print (f 3))";
    CHECK(run_src(CLL, src, 0, 0, 0) == ZL_OK && strcmp(out, "\n(3 2 1) ") == 0,
          "Common Lisp runs a LISP program");
}

/* (read): the next integer of the input (sbcl reads stdin); none left
 * traps INPUT where sbcl reads end of file. */
static void test_commonlisp_input(void)
{
    const int64_t in[] = {7, 1};
    expect_in(CLL, "(print (read))\n", in, 1, "\n7 ", "read");
    expect_in(CLL, "(case (read)\n  (1 (princ 'one))\n  (otherwise (princ 'other)))\n", in + 1, 1,
              "ONE", "read as a case key");
    expect_in(CLL, "(defun f () (read))\n(print (- (f) (f)))\n", in, 2, "\n6 ",
              "arguments read in order");
    expect_twin(CLL, "(PRINT (READ))\n", "(print (read))\n");
    expect_trap(CLL, "(print (read))\n", ZL_E_INPUT, 1, "read with no input");
    int s = run_src(CLL, "(print (read))\n(print (read))\n(print (read))\n", in, 2, 0);
    CHECK(s == ZL_E_INPUT && R.trap_line == 3 && strcmp(out, "\n7 \n1 ") == 0,
          "read past the last input: %d [%s]", s, out);
    static const lref_t rf[] = {
        {"(print (read t))", ZL_E_UNSUPPORTED, 1},
        {"(print (read-line))", ZL_E_UNSUPPORTED, 1},
        {"(print (read-from-string \"(1)\"))", ZL_E_UNSUPPORTED, 1},
    };
    lisp_refusals(CLL, rf, sizeof rf / sizeof rf[0]);
}

static void test_commonlisp(void)
{
    test_commonlisp_cases();
    test_commonlisp_refusals();
    test_commonlisp_traps();
    test_commonlisp_twins();
    test_commonlisp_input();
}
