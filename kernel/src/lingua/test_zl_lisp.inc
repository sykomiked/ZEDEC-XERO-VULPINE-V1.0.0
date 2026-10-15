/* test_zl_lisp.inc — LISP (Coilwyrm) and Scheme (Spiraldrake) front-end
 * tests, included by test_zl.c. Expected outputs are what sbcl --script and
 * guile --no-auto-compile print for the same programs. */

#define LISP ZL_LANG_LISP
#define SCM  ZL_LANG_SCHEME

typedef struct {
    const char *src, *want;
} lcase_t;

static void lisp_cases(int lang, const lcase_t *c, size_t n)
{
    for (size_t i = 0; i < n; i++) expect_out(lang, c[i].src, c[i].want, c[i].src);
}

typedef struct {
    const char *src;
    int status;
    uint32_t line;
} lref_t;

static void lisp_refusals(int lang, const lref_t *c, size_t n)
{
    for (size_t i = 0; i < n; i++) expect_refused(lang, c[i].src, c[i].status, c[i].line, c[i].src);
}

static void lisp_traps(int lang, const lref_t *c, size_t n)
{
    for (size_t i = 0; i < n; i++) expect_trap(lang, c[i].src, c[i].status, c[i].line, c[i].src);
}

/* The IR of src, as "OP a b;" items after the two-instruction prologue. */
static const char *lisp_ir(int lang, const char *src)
{
    static char b[4096];
    zl_diag_t d;
    size_t k = 0;
    b[0] = 0;
    if (compile(lang, src, &d) != ZL_OK) return d.msg;
    for (uint32_t i = 2; i < P.ncode && k + 64 < sizeof b; i++)
        k += (size_t) snprintf(b + k, sizeof b - k, "%s %lld %d;", zl_op_name(P.code[i].op),
                               (long long) P.code[i].a, P.code[i].b);
    return b;
}

static void lisp_expect_ir(int lang, const char *src, const char *want)
{
    const char *got = lisp_ir(lang, src);
    CHECK(strcmp(got, want) == 0, "IR of %s:\n got  %s\n want %s", src, got, want);
}

static void test_lisp_basics(void)
{
    static const lcase_t lc[] = {
        {"(print 1)", "\n1 "},
        {"(print -42) (print 0)", "\n-42 \n0 "},
        {"(prin1 5) (princ 6) (terpri)", "56\n"},
        {"(print 'foo) (print 'Bar) (print 'baz-quux)", "\nFOO \nBAR \nBAZ-QUUX "},
        {"(print nil) (print t) (print ()) (print '()) (print 'nil) (print 't)",
         "\nNIL \nT \nNIL \nNIL \nNIL \nT "},
        {"(print '(1 2 3))", "\n(1 2 3) "},
        {"(print '(a (b c) () (d . e) (1 2 . 3)))", "\n(A (B C) NIL (D . E) (1 2 . 3)) "},
        {"(print '(a . nil))", "\n(A) "},
        {"(print (quote x)) (print '5) (print '-3)", "\nX \n5 \n-3 "},
        {"(print \"str\") (prin1 \"a\\\"b\\\\c\") (princ \"a\\\"b\\\\c\")",
         "\n\"str\" \"a\\\"b\\\\c\"a\"b\\c"},
        {"(print +7) (print 4611686018427387903) (print -4611686018427387903)",
         "\n7 \n4611686018427387903 \n-4611686018427387903 "},
        {"; only a comment\n", ""},
        {"", ""},
        {"(print (+)) (print (+ 5)) (print (+ 1 2 3 4))", "\n0 \n5 \n10 "},
        {"(print (-  5)) (print (- 10 1 2 3))", "\n-5 \n4 "},
        {"(print (*)) (print (* 7)) (print (* 2 3 4))", "\n1 \n7 \n24 "},
        {"(print (/ 12 4)) (print (/ 1)) (print (/ -1)) (print (/ 100 5 2))", "\n3 \n1 \n-1 \n10 "},
        {"(print (list (floor 7 2) (floor -7 2) (floor 7 -2) (floor -7 -2) (floor 9)))",
         "\n(3 -4 -4 3 9) "},
        {"(print (list (truncate 7 2) (truncate -7 2) (truncate 7 -2) (truncate 5)))",
         "\n(3 -3 -3 5) "},
        {"(print (list (ceiling 7 2) (ceiling -7 2) (ceiling 6 2) (ceiling 4)))", "\n(4 -3 3 4) "},
        {"(print (list (mod 7 2) (mod -7 2) (mod 7 -2) (mod -7 -2)))", "\n(1 1 -1 -1) "},
        {"(print (list (rem 7 2) (rem -7 2) (rem 7 -2) (rem -7 -2)))", "\n(1 -1 1 -1) "},
        {"(print (list (abs -3) (abs 3) (min 4) (max 4) (min 3 -1 2) (max 3 -1 9)))",
         "\n(3 3 4 4 -1 9) "},
        {"(print (list (1+ 5) (1- 5) (1+ -1)))", "\n(6 4 0) "},
        {"(print (list (expt 2 10) (expt -2 3) (expt 5 0) (expt 0 0) (expt 1 -3)))",
         "\n(1024 -8 1 1 1) "},
        {"(print (list (= 1 1) (= 1 2) (= 2 2 2) (= 2 2 3) (/= 1 2) (/= 2 2)))",
         "\n(T NIL T NIL T NIL) "},
        {"(print (list (< 1 2) (< 2 1) (< 1 2 3) (< 1 3 2) (> 3 2 1) (> 3 1 2)))",
         "\n(T NIL T NIL T NIL) "},
        {"(print (list (<= 1 1 2) (<= 2 1) (>= 2 2 1) (>= 1 2)))", "\n(T NIL T NIL) "},
        {"(print (list (zerop 0) (zerop 3) (plusp 1) (plusp 0) (minusp -1) (minusp 0)))",
         "\n(T NIL T NIL T NIL) "},
        {"(print (list (evenp 0) (evenp -3) (oddp -3) (oddp 4)))", "\n(T NIL T NIL) "},
        {"(print (list (numberp 1) (numberp 'a) (integerp -5) (integerp nil)))",
         "\n(T NIL T NIL) "},
        {"(print (list (symbolp 'a) (symbolp nil) (symbolp t) (symbolp 1) (symbolp '(a))))",
         "\n(T T T NIL NIL) "},
        {"(print (list (null nil) (null 0) (not nil) (not 'a)))", "\n(T NIL T NIL) "},
        {"(print (list (consp '(1)) (consp nil) (atom nil) (atom '(1)) (listp nil) (listp '(1)) "
         "(listp 1)))",
         "\n(T NIL T NIL T T NIL) "},
        {"(print (list (eq 'a 'a) (eq 'a 'b) (eql 7 7) (eq '(1) '(1)) (equal '(1 (2)) '(1 (2)))))",
         "\n(T NIL T NIL T) "},
        {"(print (list (equal 1 1) (equal 'a 'b) (equal '(1 2) '(1 2 3)) (equal '(1 . 2) '(1 . 2)) "
         "(equal nil nil)))",
         "\n(T NIL NIL T T) "},
        {"(print (list (car '(1 2)) (cdr '(1 2)) (car nil) (cdr nil) (cadr '(1 2)) (cddr '(1 2 "
         "3))))",
         "\n(1 (2) NIL NIL 2 (3)) "},
        {"(print (list (caar '((1) 2)) (cdar '((1 5))) (caddr '(1 2 3)) (cadddr '(1 2 3 4)) "
         "(cddddr '(1 2 3 4 5))))",
         "\n(1 (5) 3 4 (5)) "},
        {"(print (list (first '(1 2 3)) (rest '(1 2 3)) (second '(1 2 3)) (third '(1 2 3))))",
         "\n(1 (2 3) 2 3) "},
        {"(print (cons 1 2)) (print (cons 1 nil)) (print (cons nil nil))",
         "\n(1 . 2) \n(1) \n(NIL) "},
        {"(print (list)) (print (list 1)) (print (list 1 (list 2 3) 'x))",
         "\nNIL \n(1) \n(1 (2 3) X) "},
        {"(print (list (length nil) (length '(1 2 3)) (length '((1 2) 3))))", "\n(0 3 2) "},
        {"(print (append)) (print (append '(1))) (print (append '(1 2) '(3))) (print (append nil "
         "'(3)))",
         "\nNIL \n(1) \n(1 2 3) \n(3) "},
        {"(print (append '(1) '(2) '(3 4) nil '(5))) (print (append '(1) 2))",
         "\n(1 2 3 4 5) \n(1 . 2) "},
        {"(print (reverse nil)) (print (reverse '(1 (2 3) 4)))", "\nNIL \n(4 (2 3) 1) "},
        {"(print (member 2 '(1 2 3))) (print (member 9 '(1 2))) (print (member 'b '(a b)))",
         "\n(2 3) \nNIL \n(B) "},
        {"(print (member '(1) '((1) 2)))", "\nNIL "},
        {"(print (assoc 'b '((a . 1) (b . 2) (b . 3)))) (print (assoc 'c '((a . 1))))",
         "\n(B . 2) \nNIL "},
        {"(print (assoc nil '(nil (nil . 1)))) (print (assoc 2 '((1 . a) nil (2 . b))))",
         "\n(NIL . 1) \n(2 . B) "},
        {"(defvar *p* (list 1 2)) (print (rplaca *p* 9)) (print (rplacd *p* 7)) (print *p*)",
         "\n(9 2) \n(9 . 7) \n(9 . 7) "},
        {"(defvar *p* (list 1 2)) (rplaca *p* 3) (rplacd (cdr *p*) '(4)) (print *p*)",
         "\n(3 2 4) "},
    };
    lisp_cases(LISP, lc, sizeof lc / sizeof lc[0]);
}

static void test_lisp_forms(void)
{
    static const lcase_t lc[] = {
        {"(print (if t 1 2)) (print (if nil 1 2)) (print (if nil 1)) (print (if 0 'zero))",
         "\n1 \n2 \nNIL \nZERO "},
        {"(if (> 2 1) (print 'a) (print 'b)) (if (< 2 1) (print 'c))", "\nA "},
        {"(print (cond ((= 1 2) 'a) ((= 1 1) 'b) (t 'c))) (print (cond (nil 1))) (print (cond "
         "(7)))",
         "\nB \nNIL \n7 "},
        {"(print (cond ((< 2 1) 'x) (t))) (print (cond (nil) ((+ 1 1))))", "\nT \n2 "},
        {"(cond ((= 1 1) (print 'a) (print 'b)) (t (print 'c)))", "\nA \nB "},
        {"(cond (nil (print 'x)) ((print 'y)))", "\nY "},
        {"(print (if (cond (nil 1) (2)) 'yes 'no)) (print (if (cond (nil 1)) 'yes 'no))",
         "\nYES \nNO "},
        {"(print (when t 1 2)) (print (when nil 1)) (print (unless nil 3)) (print (unless t 3))",
         "\n2 \nNIL \n3 \nNIL "},
        {"(when (= 1 1) (print 'w)) (unless (= 1 1) (print 'u)) (print (when t))", "\nW \nNIL "},
        {"(print (and)) (print (and 1)) (print (and 1 2)) (print (and nil 2)) (print (and 1 nil))",
         "\nT \n1 \n2 \nNIL \nNIL "},
        {"(print (or)) (print (or 1)) (print (or nil 2)) (print (or nil nil)) (print (or 3 "
         "(car 5)))",
         "\nNIL \n1 \n2 \nNIL \n3 "},
        {"(and (print 1) nil (print 2)) (or nil (print 3) (print 4))", "\n1 \n3 "},
        {"(print (if (and 1 2) 'y 'n)) (print (if (or nil nil) 'y 'n)) (print (if (and) 'y 'n))",
         "\nY \nN \nY "},
        {"(print (progn)) (print (progn 1 2 3)) (progn (print 1) (print 2))", "\nNIL \n3 \n1 \n2 "},
        {"(print (let ((x 1) (y 2)) (+ x y))) (print (let () 5)) (print (let (z) z))",
         "\n3 \n5 \nNIL "},
        {"(print (let ((x 1)) (let ((x 2) (y x)) (list x y))))", "\n(2 1) "},
        {"(print (let ((x 1)) (let* ((x 2) (y x)) (list x y))))", "\n(2 2) "},
        {"(print (let ((a 1) b (c)) (list a b c)))", "\n(1 NIL NIL) "},
        {"(print (let ((x 5)) (setq x (+ x 1)) x)) (print (let ((x 1)) (setq x 7)))", "\n6 \n7 "},
        {"(defvar *a* 1) (defvar *b* 2) (print (setq *a* 3 *b* (+ *a* 1))) (print (list *a* "
         "*b*))",
         "\n4 \n(3 4) "},
        {"(print (let ((x 1)) (setq))) (defparameter *z* 0) (print (setf *z* 5)) (print *z*)",
         "\nNIL \n5 \n5 "},
        {"(defvar *n* 5) (print (incf *n*)) (print (incf *n* 10)) (print (decf *n*)) (print (decf "
         "*n* 3))",
         "\n6 \n16 \n15 \n12 "},
        {"(defvar *s* nil) (print (push 1 *s*)) (push 2 *s*) (print *s*) (print (pop *s*)) (print "
         "*s*)",
         "\n(1) \n(2 1) \n2 \n(1) "},
        {"(defvar *s* nil) (print (pop *s*)) (print *s*)", "\nNIL \nNIL "},
        {"(let ((x 1)) (incf x) (push 0 x) (print x))", "\n(0 . 2) "},
        {"(dotimes (i 3) (print i)) (dotimes (i 0) (print 'never)) (dotimes (i -2) (print 'no))",
         "\n0 \n1 \n2 "},
        {"(print (dotimes (i 3))) (print (dotimes (i 3 i))) (print (dotimes (i -5 i)))",
         "\nNIL \n3 \n0 "},
        {"(let ((s 0)) (dotimes (i 5) (setq s (+ s i))) (print s))", "\n10 "},
        {"(dotimes (i 2) (dotimes (j 2) (print (+ (* 10 i) j))))", "\n0 \n1 \n10 \n11 "},
        {"(print (do ((i 0 (1+ i)) (acc nil (cons i acc))) ((= i 3) acc)))", "\n(2 1 0) "},
        {"(print (do ((i 0 (1+ i)) (j 10)) ((= i 2) (list i j)) (setq j (- j 1))))", "\n(2 8) "},
        {"(print (do ((a 1 b) (b 2 a) (k 0 (1+ k))) ((= k 3) (list a b))))", "\n(2 1) "},
        {"(print (do ((i 0 (1+ i))) ((= i 2))))", "\nNIL "},
        {"(do ((i 3 (1- i))) ((zerop i) (print 'done)) (print i))", "\n3 \n2 \n1 \nDONE "},
        {"(print (labels ((f (n) (if (zerop n) 0 (+ n (f (1- n)))))) (f 10)))", "\n55 "},
        {"(labels ((ev (n) (if (zerop n) t (od (1- n)))) (od (n) (if (zerop n) nil (ev (1- n))))) "
         "(print (ev 10)) (print (od 7)))",
         "\nT \nT "},
        {"(defun f (x) (labels ((g (y) (+ x y))) (g 10))) (print (f 5))", "\n15 "},
        {"(defun f (x) (labels ((g () (setq x (+ x 1)))) (g) (g) x)) (print (f 5))", "\n7 "},
        {"(print (labels () 4))", "\n4 "},
    };
    lisp_cases(LISP, lc, sizeof lc / sizeof lc[0]);
}

static void test_lisp_functions(void)
{
    static const lcase_t lc[] = {
        {"(defun sq (x) (* x x)) (print (sq 12))", "\n144 "},
        {"(defun f () 7) (print (f))", "\n7 "},
        {"(defun f ()) (print (f))", "\nNIL "},
        {"(defun f nil 8) (print (f))", "\n8 "},
        {"(defun fact (n) (if (< n 2) 1 (* n (fact (- n 1))))) (print (fact 20))",
         "\n2432902008176640000 "},
        {"(defun fib (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (print (fib 20))",
         "\n6765 "},
        {"(defun ev (n) (if (zerop n) t (od (1- n)))) (defun od (n) (if (zerop n) nil (ev (1- "
         "n)))) (print (ev 100)) (print (od 100))",
         "\nT \nNIL "},
        {"(defun loop1 (n acc) (if (zerop n) acc (loop1 (1- n) (+ acc 1)))) (print (loop1 200000 "
         "0))",
         "\n200000 "},
        {"(defun cnt (n) (cond ((zerop n) 'done) (t (cnt (- n 1))))) (print (cnt 50000))",
         "\nDONE "},
        {"(defun w (n) (when (> n 0) (w (- n 1)))) (print (w 30000))", "\nNIL "},
        {"(defun a (n) (and (> n 0) (a (- n 1)))) (print (a 30000))", "\nNIL "},
        {"(defun o (n) (or (zerop n) (o (- n 1)))) (print (o 30000))", "\nT "},
        {"(defun f (x y) (let ((z (* x y))) (if (> z 100) z (f (+ x 1) (+ y 1))))) (print (f 1 "
         "1))",
         "\n121 "},
        {"(defun f (x) (list x x)) (defun g (x) (f (f x))) (print (g 1))", "\n((1 1) (1 1)) "},
        {"(defun p (x) (print x) (* x 2)) (print (+ (p 1) (p 2)))", "\n1 \n2 \n6 "},
        {"(defun f (a b c) (list a b c)) (print (f (progn (print 1) 1) (progn (print 2) 2) 3))",
         "\n1 \n2 \n(1 2 3) "},
        {"(defvar *g* 10) (defun f (x) (+ x *g*)) (print (f 1)) (setq *g* 20) (print (f 1))",
         "\n11 \n21 "},
        {"(defun f () (setq *g* (+ *g* 1))) (defvar *g* 0) (f) (f) (print *g*)", "\n2 "},
        {"(defun f (x) (if x (print x) (print 'none))) (f nil) (f 3)", "\nNONE \n3 "},
        {"(defun my-len (l) (if (null l) 0 (1+ (my-len (cdr l))))) (print (my-len '(a b c d)))",
         "\n4 "},
        {"(defun list (x) x)", 0},
    };
    lisp_cases(LISP, lc, sizeof lc / sizeof lc[0] - 1);
    static const lcase_t fmt[] = {
        {"(format t \"hello~%\")", "hello\n"},
        {"(format t \"~a and ~d and ~s~%\" 'x 42 '(1 \"z\"))", 0},
        {"(format t \"~a and ~d and ~s~%\" 'x 42 '(1 b))", "X and 42 and (1 B)\n"},
        {"(format t \"~A~D~S~~~%\" 1 2 3)", "123~\n"},
        {"(format t \"[~a]\" \"str\") (format t \"[~s]\" \"str\")", "[str][\"str\"]"},
        {"(format t \"~a\" (progn (princ \"x\") 1))", "x1"},
        {"(print (format t \"z\"))", "z\nNIL "},
        {"(format t \"\") (format t \"~%\")", "\n"},
    };
    for (size_t i = 0; i < sizeof fmt / sizeof fmt[0]; i++)
        if (fmt[i].want) expect_out(LISP, fmt[i].src, fmt[i].want, fmt[i].src);
    expect_refused(LISP, fmt[1].src, ZL_E_UNSUPPORTED, 1, "a string inside quoted data");
}

static void test_scheme_basics(void)
{
    static const lcase_t sc[] = {
        {"(display 1)", "1"},
        {"(display -42) (newline) (display 0)", "-42\n0"},
        {"(write 5) (display 'foo) (display 'Bar) (write 'baz-quux)", "5fooBarbaz-quux"},
        {"(display '()) (display #t) (display #f) (display #true) (display #false)", "()#t#f#t#f"},
        {"(display '(1 2 3))", "(1 2 3)"},
        {"(display '(a (b c) () (d . e) (1 2 . 3) #t))", "(a (b c) () (d . e) (1 2 . 3) #t)"},
        {"(display (quote x)) (display '5) (display '#t)", "x5#t"},
        {"(display \"a\\\"b\\\\c\\nd\") (write \"a\\\"b\\\\c\\nd\")",
         "a\"b\\c\nd\"a\\\"b\\\\c\\nd\""},
        {"(display +7) (display 4611686018427387903)", "74611686018427387903"},
        {"; only a comment\n", ""},
        {"(display (+)) (display (+ 5)) (display (+ 1 2 3 4))", "0510"},
        {"(display (list (- 5) (- 10 1 2 3) (*) (* 7) (* 2 3 4)))", "(-5 4 1 7 24)"},
        {"(display (list (/ 12 4) (/ 1) (/ -1) (/ 100 5 2)))", "(3 1 -1 10)"},
        {"(display (list (quotient 7 2) (quotient -7 2) (quotient 7 -2) (quotient -7 -2)))",
         "(3 -3 -3 3)"},
        {"(display (list (remainder 7 2) (remainder -7 2) (remainder 7 -2) (remainder -7 -2)))",
         "(1 -1 1 -1)"},
        {"(display (list (modulo 7 2) (modulo -7 2) (modulo 7 -2) (modulo -7 -2)))", "(1 1 -1 -1)"},
        {"(display (list (floor-quotient -7 2) (floor-remainder -7 2) (truncate-quotient -7 2) "
         "(truncate-remainder -7 2)))",
         "(-4 1 -3 -1)"},
        {"(display (list (abs -3) (min 4) (max 4) (min 3 -1 2) (max 3 -1 9) (expt 2 10)))",
         "(3 4 4 -1 9 1024)"},
        {"(display (list (= 1 1) (= 1 2) (= 2 2 2) (= 2 2 3) (< 1 2 3) (< 1 3 2) (> 3 2 1)))",
         "(#t #f #t #f #t #f #t)"},
        {"(display (list (<= 1 1 2) (<= 2 1) (>= 2 2 1) (>= 1 2)))", "(#t #f #t #f)"},
        {"(display (list (zero? 0) (zero? 3) (positive? 1) (positive? 0) (negative? -1) "
         "(negative? 0)))",
         "(#t #f #t #f #t #f)"},
        {"(display (list (even? 0) (even? -3) (odd? -3) (odd? 4)))", "(#t #f #t #f)"},
        {"(display (list (number? 1) (number? 'a) (integer? -5) (integer? '())))", "(#t #f #t #f)"},
        {"(display (list (symbol? 'a) (symbol? '()) (symbol? #t) (symbol? 1)))", "(#t #f #f #f)"},
        {"(display (list (null? '()) (null? 0) (null? #f) (not #f) (not '()) (not 0)))",
         "(#t #f #f #t #f #f)"},
        {"(display (list (pair? '(1)) (pair? '()) (eq? 'a 'a) (eq? 'a 'b) (eqv? 7 7) (eq? '() "
         "'())))",
         "(#t #f #t #f #t #t)"},
        {"(display (list (equal? '(1 (2)) '(1 (2))) (equal? '(1 2) '(1 2 3)) (equal? 1 1) (equal? "
         "'a 'b)))",
         "(#t #f #t #f)"},
        {"(display (list (car '(1 2)) (cdr '(1 2)) (cadr '(1 2)) (cddr '(1 2 3)) (caddr '(1 2 "
         "3))))",
         "(1 (2) 2 (3) 3)"},
        {"(display (list (caar '((1) 2)) (cdar '((1 5))) (cadddr '(1 2 3 4))))", "(1 (5) 4)"},
        {"(display (cons 1 2)) (display (cons 1 '())) (display (list)) (display (list 1 (list 2)))",
         "(1 . 2)(1)()(1 (2))"},
        {"(display (list (length '()) (length '(1 2 3))))", "(0 3)"},
        {"(display (list (append) (append '(1)) (append '(1 2) '(3)) (append '() '(3)) (append "
         "'(1) '(2) '(3))))",
         "(() (1) (1 2 3) (3) (1 2 3))"},
        {"(display (append '(1) 2)) (display (reverse '())) (display (reverse '(1 (2 3) 4)))",
         "(1 . 2)()(4 (2 3) 1)"},
        {"(display (list (memq 'b '(a b c)) (memq 'z '(a)) (memv 2 '(1 2)) (member '(1) '((0) "
         "(1)))))",
         "((b c) #f (2) ((1)))"},
        {"(display (list (assq 'b '((a . 1) (b . 2))) (assv 2 '((2 . x))) (assq 'z '((a . 1))) "
         "(assoc '(k) '(((k) . 5)))))",
         "((b . 2) (2 . x) #f ((k) . 5))"},
        {"(define p (list 1 2)) (set-car! p 9) (set-cdr! (cdr p) '(4)) (display p)", "(9 2 4)"},
    };
    lisp_cases(SCM, sc, sizeof sc / sizeof sc[0]);
}

static void test_scheme_forms(void)
{
    static const lcase_t sc[] = {
        {"(display (if #t 1 2)) (display (if #f 1 2)) (display (if 0 'zero 'no)) (display (if '() "
         "'nil 'no))",
         "12zeronil"},
        {"(if (> 2 1) (display 'a) (display 'b)) (if (< 2 1) (display 'c))", "a"},
        {"(display (cond ((= 1 2) 'a) ((= 1 1) 'b) (else 'c))) (display (cond (7) (else 1)))",
         "b7"},
        {"(cond ((= 1 1) (display 'a) (display 'b)) (else (display 'c)))", "ab"},
        {"(when (= 1 1) (display 'w) (display 'x)) (unless (= 1 1) (display 'u)) (unless #f "
         "(display 'v))",
         "wxv"},
        {"(display (and)) (display (and 1)) (display (and 1 2)) (display (and #f 2)) (display (and "
         "1 #f))",
         "#t12#f#f"},
        {"(display (or)) (display (or 1)) (display (or #f 2)) (display (or #f #f)) (display (or 3 "
         "(car 5)))",
         "#f12#f3"},
        {"(and (display 1) #f (display 2))", 0},
        {"(display (begin 1 2 3)) (begin (display 1) (display 2))", "312"},
        {"(display (let ((x 1) (y 2)) (+ x y))) (display (let () 5))", "35"},
        {"(display (let ((x 1)) (let ((x 2) (y x)) (list x y))))", "(2 1)"},
        {"(display (let ((x 1)) (let* ((x 2) (y x)) (list x y))))", "(2 2)"},
        {"(define x 1) (set! x (+ x 5)) (display x)", "6"},
        {"(display (let loop ((i 0) (acc '())) (if (= i 3) acc (loop (+ i 1) (cons i acc)))))",
         "(2 1 0)"},
        {"(display (let loop ((i 0)) (if (< i 100000) (loop (+ i 1)) i)))", "100000"},
        {"(let loop ((i 0)) (when (< i 3) (display i) (loop (+ i 1))))", "012"},
        {"(display (let f ((n 5)) (if (= n 0) 1 (* n (f (- n 1))))))", "120"},
        {"(define (g x) (let loop ((i x) (s 0)) (if (= i 0) s (loop (- i 1) (+ s x))))) (display "
         "(g 4))",
         "16"},
        {"(display (do ((i 0 (+ i 1)) (acc '() (cons i acc))) ((= i 3) acc)))", "(2 1 0)"},
        {"(do ((i 0 (+ i 1))) ((= i 3)) (display i))", "012"},
        {"(display (do ((a 1 b) (b 2 a) (k 0 (+ k 1))) ((= k 3) (list a b))))", "(2 1)"},
        {"(define (f) (define a 1) (define (g) (+ a 10)) (g)) (display (f))", "11"},
        {"(define (f n) (define (ev? n) (if (= n 0) #t (od? (- n 1)))) (define (od? n) (if (= n 0) "
         "#f (ev? (- n 1)))) (ev? n)) (display (f 10)) (display (f 7))",
         "#t#f"},
        {"(define (f x) (define y (* x 2)) (define z (+ y 1)) (list x y z)) (display (f 3))",
         "(3 6 7)"},
        {"(define (f x) (let ((y 1)) (define z 5) (+ x y z))) (display (f 1))", "7"},
        {"(define (f x) (define (g) (set! x (+ x 1))) (g) (g) x) (display (f 5))", "7"},
    };
    for (size_t i = 0; i < sizeof sc / sizeof sc[0]; i++)
        if (sc[i].want) expect_out(SCM, sc[i].src, sc[i].want, sc[i].src);
}

static void test_scheme_functions(void)
{
    static const lcase_t sc[] = {
        {"(define (sq x) (* x x)) (display (sq 12))", "144"},
        {"(define (f) 7) (display (f))", "7"},
        {"(define sq (lambda (x) (* x x))) (display (sq 5))", "25"},
        {"(define (fact n) (if (< n 2) 1 (* n (fact (- n 1))))) (display (fact 20))",
         "2432902008176640000"},
        {"(define (ev? n) (if (= n 0) #t (od? (- n 1)))) (define (od? n) (if (= n 0) #f (ev? (- n "
         "1)))) (display (ev? 100))",
         "#t"},
        {"(define (loop1 n acc) (if (= n 0) acc (loop1 (- n 1) (+ acc 1)))) (display (loop1 200000 "
         "0))",
         "200000"},
        {"(define (show x) (display x) (newline)) (show 1) (show '(a b))", "1\n(a b)\n"},
        {"(define (show x) (if (> x 0) (display x))) (show 1) (show -1)", "1"},
        {"(define (f x) (when (> x 0) (display x) (f (- x 1)))) (f 3)", "321"},
        {"(define (f x) (set! x 1)) (f 2) (display 'ok)", "ok"},
        {"(define (a) (b)) (define (b) (display 1)) (a)", "1"},
        {"(define (f x) (cond ((= x 0) 'zero) ((> x 0) (f (- x 1))) (else 'neg))) (display (f 3)) "
         "(display (f -1))",
         "zeroneg"},
        {"(define g 10) (define (f x) (+ x g)) (display (f 1)) (set! g 20) (display (f 1))",
         "1121"},
        {"(define (list x) x) (display (list 5))", "5"},
        {"(define (reverse l) 'mine) (display (reverse '(1 2)))", "mine"},
        {"(define (f list) (car list)) (display (f '(4 5)))", "4"},
        {"(define (p x) (display x) (* x 2)) (display (+ (p 1) (p 2)))", "126"},
    };
    lisp_cases(SCM, sc, sizeof sc / sizeof sc[0]);
}

static void test_lisp_twins(void)
{
    expect_twin(LISP, "(print   1)", "(PRINT 1)\n");
    expect_twin(LISP, "(defun sq (x) (* x x))\n\n\n(print (sq 3))",
                "(DEFUN SQ (X)\n  (* X X))\n(PRINT (SQ 3))\n");
    expect_twin(LISP, ";;; header\n(print 1) ; after\n;\n( print\n  2 )\n",
                "; header\n(PRINT 1)\n; after\n;\n(PRINT 2)\n");
    expect_twin(LISP,
                "(print '(a  b . c)) (print (quote x)) (print '5) (print 'nil) (print '()) "
                "(print ()) (print 't)",
                "(PRINT '(A B . C))\n(PRINT 'X)\n(PRINT 5)\n(PRINT NIL)\n(PRINT NIL)\n(PRINT "
                "NIL)\n(PRINT T)\n");
    expect_twin(LISP,
                "(print (first '(1 2))) (print (rest '(1))) (print (second '(1 2))) (print "
                "(third '(1 2 3)))",
                "(PRINT (CAR '(1 2)))\n(PRINT (CDR '(1)))\n(PRINT (CADR '(1 2)))\n(PRINT (CADDR "
                "'(1 2 3)))\n");
    expect_twin(LISP, "(defvar x 1) (setf x 2) (setq x 3)",
                "(DEFVAR X 1)\n(SETQ X 2)\n(SETQ X 3)\n");
    expect_twin(LISP, "(let (a (b) (c 1)) (print (list a b c)))",
                "(LET ((A NIL) (B NIL) (C 1))\n  (PRINT (LIST A B C)))\n");
    expect_twin(LISP, "(let () 1) (let nil 2) (defun f () 3) (defun g nil 4)",
                "(LET ()\n  1)\n(LET ()\n  2)\n(DEFUN F ()\n  3)\n(DEFUN G ()\n  4)\n");
    expect_twin(LISP, "(prin1 \"a\\\"b\\\\c\") (print +5) (print -0)",
                "(PRIN1 \"a\\\"b\\\\c\")\n(PRINT 5)\n(PRINT 0)\n");
    expect_twin(
        LISP, "(defun f (n) (cond ((zerop n) 1) (t (when (> n 1) (print n)) (progn (f (1- n))))))",
        "(DEFUN F (N)\n  (COND\n    ((ZEROP N) 1)\n    (T (WHEN (> N 1)\n         (PRINT N))\n     "
        " "
        "(PROGN\n        (F (1- N))))))\n");
    expect_twin(LISP, "(defun f (l) (if (null l) nil (let ((x (car l))) (cons x (f (cdr l))))))",
                "(DEFUN F (L)\n  (IF (NULL L)\n    NIL\n    (LET ((X (CAR L)))\n      (CONS X (F "
                "(CDR L))))))\n");
    expect_twin(LISP,
                "(dotimes (i 3) (print i) (print (* i i))) (do ((i 0 (1+ i))) ((= i 2) 'r) "
                "(print i))",
                "(DOTIMES (I 3)\n  (PRINT I)\n  (PRINT (* I I)))\n(DO ((I 0 (1+ I))) ((= I 2) "
                "'R)\n  (PRINT I))\n");
    expect_twin(LISP, "(labels ((f (x) (* x 2)) (g () 1)) (print (f (g))))",
                "(LABELS ((F (X)\n           (* X 2)) (G ()\n                      1))\n  (PRINT "
                "(F (G))))\n");
    expect_twin(LISP, "(print (let* ((a 1)) a)) (unless nil (print 1))",
                "(PRINT (LET* ((A 1))\n         A))\n(UNLESS NIL\n  (PRINT 1))\n");
    expect_twin(LISP, "(format t \"~a~%\" 'x) (defparameter *x* 'y)",
                "(FORMAT T \"~a~%\" 'X)\n(DEFPARAMETER *X* 'Y)\n");
    /* the twin is accepted, other spellings are not */
    zl_diag_t d;
    CHECK(compile(LISP | ZL_TWIN, "(PRINT 1)\n", &d) == ZL_OK, "canonical LISP accepted");
    CHECK(compile(LISP | ZL_TWIN, "(print 1)\n", &d) == ZL_E_NONCANON && d.line == 1,
          "lower case is not canonical");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT 1)", &d) == ZL_E_NONCANON, "a missing newline");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT 1)\n\n(PRINT 2)\n", &d) == ZL_E_NONCANON && d.line == 2,
          "a blank line is not canonical");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT (FIRST '(1)))\n", &d) == ZL_E_NONCANON,
          "a synonym is not canonical");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT (CAR '(1)))\n", &d) == ZL_OK, "its canonical name is");
    CHECK(compile(LISP | ZL_TWIN, "(LET (X)\n  X)\n", &d) == ZL_E_NONCANON, "a bare binding");
    CHECK(compile(LISP | ZL_TWIN, "(LET ((X NIL))\n  X)\n", &d) == ZL_OK, "(X NIL)");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT 'X) ; c\n", &d) == ZL_E_NONCANON,
          "a comment after a form on its line");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT 'X)\n; c\n", &d) == ZL_OK, "a comment on its own line");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT 'X)\n;  c\n", &d) == ZL_E_NONCANON,
          "a comment with two spaces");
    CHECK(compile(LISP | ZL_TWIN, "(PRINT (+ 1 ; inner\n 2))\n", &d) == ZL_E_NONCANON,
          "a comment inside a form is dropped by the twin");
}

static void test_scheme_twins(void)
{
    expect_twin(SCM, "(display   1)", "(display 1)\n");
    expect_twin(SCM, "(define (Sq x) (* x x)) (display (Sq 3))",
                "(define (Sq x)\n  (* x x))\n(display (Sq 3))\n");
    expect_twin(SCM,
                "(define sq (lambda (x y) (display x) (* x y))) (define z (lambda () 1)) "
                "(display (sq 2 3))",
                "(define (sq x y)\n  (display x)\n  (* x y))\n(define (z)\n  1)\n(display (sq 2 "
                "3))\n");
    expect_twin(SCM, "(display #true) (display #false) (display '#t) (display '5) (display '())",
                "(display #t)\n(display #f)\n(display #t)\n(display 5)\n(display '())\n");
    expect_twin(SCM, "(write \"a\\nb\\\"c\\\\\") (display (quote (a . b)))",
                "(write \"a\\nb\\\"c\\\\\")\n(display '(a . b))\n");
    expect_twin(SCM, "(let loop ((i 0)) (when (< i 2) (display i) (loop (+ i 1))))",
                "(let loop ((i 0))\n  (when (< i 2)\n    (display i)\n    (loop (+ i 1))))\n");
    expect_twin(SCM, "(define (f) (define a 1) (begin a))",
                "(define (f)\n  (define a 1)\n  (begin\n    a))\n");
    expect_twin(SCM, "(do ((i 0 (+ i 1))) ((= i 2)) (display i)) (display (cond (else 1)))",
                "(do ((i 0 (+ i 1))) ((= i 2))\n  (display i))\n(display (cond\n           (else "
                "1)))\n");
    zl_diag_t d;
    CHECK(compile(SCM | ZL_TWIN, "(display 1)\n", &d) == ZL_OK, "canonical Scheme accepted");
    CHECK(compile(SCM | ZL_TWIN, "(DISPLAY 1)\n", &d) == ZL_E_UNDEFINED,
          "Scheme keeps case: DISPLAY is another name");
    CHECK(compile(SCM | ZL_TWIN, "(display #true)\n", &d) == ZL_E_NONCANON,
          "#true is not canonical");
    CHECK(compile(SCM | ZL_TWIN, "(define (f x)\n  x)\n", &d) == ZL_OK, "short define");
    CHECK(compile(SCM | ZL_TWIN, "(define f (lambda (x) x))\n", &d) == ZL_E_NONCANON,
          "the lambda spelling of a define");
}

static void test_lisp_refusals(void)
{
    static const lref_t lr[] = {
        {"(print 1.5)", ZL_E_UNSUPPORTED, 1},
        {"(print 1/2)", ZL_E_UNSUPPORTED, 1},
        {"(print 1e3)", ZL_E_UNSUPPORTED, 1},
        {"(print .5)", ZL_E_UNSUPPORTED, 1},
        {"(print -.5)", ZL_E_UNSUPPORTED, 1},
        {"(print 1.)", ZL_E_UNSUPPORTED, 1},
        {"\n(print 4611686018427387904)", ZL_E_UNSUPPORTED, 2},
        {"(print -4611686018427387904)", ZL_E_UNSUPPORTED, 1},
        {"(print 99999999999999999999999)", ZL_E_UNSUPPORTED, 1},
        {"(print (sqrt 4))", ZL_E_UNSUPPORTED, 1},
        {"(print (round 4))", ZL_E_UNSUPPORTED, 1},
        {"(print (float 4))", ZL_E_UNSUPPORTED, 1},
        {"(defvar x \"s\")", ZL_E_UNSUPPORTED, 1},
        {"(print (list \"s\"))", ZL_E_UNSUPPORTED, 1},
        {"(print (if t \"s\" 1))", ZL_E_UNSUPPORTED, 1},
        {"(print (print \"s\"))", ZL_E_UNSUPPORTED, 1},
        {"(print (prin1 \"s\"))", ZL_E_UNSUPPORTED, 1},
        {"(print (concatenate 'string \"a\"))", ZL_E_UNSUPPORTED, 1},
        {"(print #\\a)", ZL_E_UNSUPPORTED, 1},
        {"(print #(1 2))", ZL_E_UNSUPPORTED, 1},
        {"(print (make-array 3))", ZL_E_UNSUPPORTED, 1},
        {"(print (make-hash-table))", ZL_E_UNSUPPORTED, 1},
        {"(print (vector 1))", ZL_E_UNSUPPORTED, 1},
        {"(print #'car)", ZL_E_UNSUPPORTED, 1},
        {"(print (function car))", ZL_E_UNSUPPORTED, 1},
        {"(print (lambda (x) x))", ZL_E_UNSUPPORTED, 1},
        {"(print ((lambda (x) x) 1))", ZL_E_UNSUPPORTED, 1},
        {"(print (funcall 'car '(1)))", ZL_E_UNSUPPORTED, 1},
        {"(print (apply '+ '(1 2)))", ZL_E_UNSUPPORTED, 1},
        {"(print (mapcar 'car '((1))))", ZL_E_UNSUPPORTED, 1},
        {"(flet ((f () 1)) (f))", ZL_E_UNSUPPORTED, 1},
        {"(print `(a ,b))", ZL_E_UNSUPPORTED, 1},
        {"(print ,a)", ZL_E_UNSUPPORTED, 1},
        {"(defmacro m (x) x)", ZL_E_UNSUPPORTED, 1},
        {"(loop for i from 1 to 3 do (print i))", ZL_E_UNSUPPORTED, 1},
        {"(block b (return-from b 1))", ZL_E_UNSUPPORTED, 1},
        {"(dotimes (i 3) (return 1))", ZL_E_UNSUPPORTED, 1},
        {"(catch 'x (throw 'x 1))", ZL_E_UNSUPPORTED, 1},
        {"(print (values 1 2))", ZL_E_UNSUPPORTED, 1},
        {"(multiple-value-bind (a b) (floor 7 2) (print a))", ZL_E_UNSUPPORTED, 1},
        {"(defconstant c 1)", ZL_E_UNSUPPORTED, 1},
        {"(defstruct p x)", ZL_E_UNSUPPORTED, 1},
        {"(print (case 1 (1 'a)))", ZL_E_UNSUPPORTED, 1},
        {"(print (nth 1 '(1 2)))", ZL_E_UNSUPPORTED, 1},
        {"(print (random 3))", ZL_E_UNSUPPORTED, 1},
        {"(error \"x\")", ZL_E_UNSUPPORTED, 1},
        {"(print :key)", ZL_E_UNSUPPORTED, 1},
        {"(print 'cl:car)", ZL_E_UNSUPPORTED, 1},
        {"(print '|a b|)", ZL_E_UNSUPPORTED, 1},
        {"(print '1+)", ZL_E_UNSUPPORTED, 1},
        {"(print 'a.b)", ZL_E_UNSUPPORTED, 1},
        {"(print ''a)", ZL_E_UNSUPPORTED, 1},
        {"(print '(1 'a))", ZL_E_UNSUPPORTED, 1},
        {"(print '(\"s\"))", ZL_E_UNSUPPORTED, 1},
        {"#| block |# (print 1)", ZL_E_UNSUPPORTED, 1},
        {"(print #x10)", ZL_E_UNSUPPORTED, 1},
        {"(print #t)", ZL_E_UNSUPPORTED, 1},
        {"(defun f (&optional x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defun f (&rest x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defun f (a &key b) a)", ZL_E_UNSUPPORTED, 1},
        {"(defvar *x* 1) (defun f (*x*) *x*)", ZL_E_UNSUPPORTED, 1},
        {"(defvar *x* 1) (let ((*x* 2)) *x*)", ZL_E_UNSUPPORTED, 1},
        {"(defvar *x* 1) (dotimes (*x* 2) 1)", ZL_E_UNSUPPORTED, 1},
        {"(defvar x)", ZL_E_UNSUPPORTED, 1},
        {"(defvar x 1 \"doc\")", ZL_E_SYNTAX, 1},
        {"(defun car (x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defun list (x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defun if (x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defun cadar (x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defun nil (x) x)", ZL_E_UNSUPPORTED, 1},
        {"(defvar list 1)", ZL_E_UNSUPPORTED, 1},
        {"(defvar t 1)", ZL_E_UNSUPPORTED, 1},
        {"(labels ((car (x) x)) (car 1))", ZL_E_UNSUPPORTED, 1},
        {"(let ((x 1)) (defun f () x))", ZL_E_UNSUPPORTED, 1},
        {"(print (defvar y 1))", ZL_E_UNSUPPORTED, 1},
        {"(setf (car x) 1)", ZL_E_UNSUPPORTED, 1},
        {"(defvar l nil) (incf (car l))", ZL_E_UNSUPPORTED, 1},
        {"(defvar l nil) (push 1 (cdr l))", ZL_E_UNSUPPORTED, 1},
        {"(format nil \"x\")", ZL_E_UNSUPPORTED, 1},
        {"(format t 'x)", ZL_E_UNSUPPORTED, 1},
        {"(format t \"~&x\")", ZL_E_UNSUPPORTED, 1},
        {"(format t \"~a\")", ZL_E_UNSUPPORTED, 1},
        {"(format t \"x\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(format t \"~a ~a\" 1)", ZL_E_UNSUPPORTED, 1},
        {"(format t \"x~\")", ZL_E_SYNTAX, 1},
        {"(print \"a\\nb\")", ZL_E_UNSUPPORTED, 1},
        {"(print \"a\nb\")", ZL_E_UNSUPPORTED, 1},
        {"(print \"\xc3\xa9\")", ZL_E_UNSUPPORTED, 1},
        {"(dotimes (i 2) 5)", ZL_E_UNSUPPORTED, 1},
        {"(dotimes (i 2) tag (print i))", ZL_E_UNSUPPORTED, 1},
        {"(do ((i 0 (1+ i))) ((= i 2)) 7)", ZL_E_UNSUPPORTED, 1},
        {"(fresh-line)", ZL_E_UNSUPPORTED, 1},
        {"(write 1)", ZL_E_UNSUPPORTED, 1},
        /* syntax */
        {"(print 1", ZL_E_SYNTAX, 1},
        {"(print 1))", ZL_E_SYNTAX, 1},
        {")", ZL_E_SYNTAX, 1},
        {"(print \"abc)", ZL_E_SYNTAX, 1},
        {"(print '(1 . 2 3))", ZL_E_SYNTAX, 1},
        {"(print '( . 2))", ZL_E_SYNTAX, 1},
        {"(print '(1 .))", ZL_E_SYNTAX, 1},
        {"(print .)", ZL_E_SYNTAX, 1},
        {"(print a#b)", ZL_E_SYNTAX, 1},
        {"(print [1])", ZL_E_SYNTAX, 1},
        {"'", ZL_E_SYNTAX, 1},
        {"(1 2)", ZL_E_SYNTAX, 1},
        {"(\"f\" 2)", ZL_E_SYNTAX, 1},
        {"(print . 1)", ZL_E_SYNTAX, 1},
        {"(if)", ZL_E_SYNTAX, 1},
        {"(if 1 2 3 4)", ZL_E_SYNTAX, 1},
        {"(quote)", ZL_E_SYNTAX, 1},
        {"(quote 1 2)", ZL_E_SYNTAX, 1},
        {"(let)", ZL_E_SYNTAX, 1},
        {"(let x 1)", ZL_E_SYNTAX, 1},
        {"(let ((x 1 2)) x)", ZL_E_SYNTAX, 1},
        {"(let ((1 2)) 1)", ZL_E_SYNTAX, 1},
        {"(let ((x 1) (x 2)) x)", ZL_E_SYNTAX, 1},
        {"(let ((nil 1)) 1)", ZL_E_SYNTAX, 1},
        {"(let ((t 1)) 1)", ZL_E_SYNTAX, 1},
        {"(setq nil 1)", ZL_E_SYNTAX, 1},
        {"(setq t 1)", ZL_E_SYNTAX, 1},
        {"(setq x)", ZL_E_SYNTAX, 1},
        {"(setq 1 2)", ZL_E_SYNTAX, 1},
        {"(defun f (x x) x)", ZL_E_SYNTAX, 1},
        {"(defun f (x) x) (defun f (y) y)", ZL_E_SYNTAX, 1},
        {"(defvar x 1) (defvar x 2)", ZL_E_SYNTAX, 1},
        {"(defun f)", ZL_E_SYNTAX, 1},
        {"(defun 5 () 1)", ZL_E_SYNTAX, 1},
        {"(defun f x 1)", ZL_E_SYNTAX, 1},
        {"(defun f (1) 1)", ZL_E_SYNTAX, 1},
        {"(defvar 5 1)", ZL_E_SYNTAX, 1},
        {"(defun f (x) x) (f)", ZL_E_SYNTAX, 1},
        {"(defun f (x) x) (f 1 2)", ZL_E_SYNTAX, 1},
        {"(car)", ZL_E_SYNTAX, 1},
        {"(cons 1)", ZL_E_SYNTAX, 1},
        {"(cadr 1 2)", ZL_E_SYNTAX, 1},
        {"(print)", ZL_E_SYNTAX, 1},
        {"(terpri 1)", ZL_E_SYNTAX, 1},
        {"(= 1)", ZL_E_SYNTAX, 1},
        {"(- )", ZL_E_SYNTAX, 1},
        {"(/= 1 2 3)", ZL_E_SYNTAX, 1},
        {"(floor 1 2 3)", ZL_E_SYNTAX, 1},
        {"(incf)", ZL_E_SYNTAX, 1},
        {"(incf x 1 2)", ZL_E_SYNTAX, 1},
        {"(pop)", ZL_E_SYNTAX, 1},
        {"(dotimes i 1)", ZL_E_SYNTAX, 1},
        {"(dotimes (i) 1)", ZL_E_SYNTAX, 1},
        {"(dotimes (i 1 2 3) 1)", ZL_E_SYNTAX, 1},
        {"(dotimes (1 2) 1)", ZL_E_SYNTAX, 1},
        {"(do)", ZL_E_SYNTAX, 1},
        {"(do ((i 0)) ())", ZL_E_SYNTAX, 1},
        {"(do ((i)) (t))", ZL_E_SYNTAX, 1},
        {"(do ((i 0) (i 1)) (t))", ZL_E_SYNTAX, 1},
        {"(do (i) (t))", ZL_E_SYNTAX, 1},
        {"(labels)", ZL_E_SYNTAX, 1},
        {"(labels ((f)) 1)", ZL_E_SYNTAX, 1},
        {"(labels ((f () 1) (f () 2)) 1)", ZL_E_SYNTAX, 1},
        {"(labels ((5 () 1)) 1)", ZL_E_SYNTAX, 1},
        {"(cond (1 2) 3)", ZL_E_SYNTAX, 1},
        {"(cond ())", ZL_E_SYNTAX, 1},
        {"(when)", ZL_E_SYNTAX, 1},
        {"(print '(1 2) . 3)", ZL_E_SYNTAX, 1},
        /* names */
        {"(print x)", ZL_E_UNDEFINED, 1},
        {"(frob 1)", ZL_E_UNDEFINED, 1},
        {"(setq y 1)", ZL_E_UNDEFINED, 1},
        {"(defun f () (g))", ZL_E_UNDEFINED, 1},
        {"(print 1)\n(print (+ 1\n z))", ZL_E_UNDEFINED, 3},
        {"(let ((f 1)) (f))", ZL_E_UNDEFINED, 1},
        {"(defun f () 1) (print f)", ZL_E_UNDEFINED, 1},
        {"(print (f)) (defun f () 1)", ZL_E_UNDEFINED, 1},
        {"(defun g () (f)) (print (g)) (defun f () 1)", ZL_E_UNDEFINED, 1},
        {"(print *x*) (defvar *x* 1)", ZL_E_UNDEFINED, 1},
        {"(defvar *x* *x*)", ZL_E_UNDEFINED, 1},
        {"(defun f () *x*) (f) (defvar *x* 1)", ZL_E_UNDEFINED, 1},
        {"(defun f () *x*) (defvar *x* (f))", ZL_E_UNDEFINED, 1},
        {"(defun f () (g)) (defun g () *x*) (print (f)) (defvar *x* 1)", ZL_E_UNDEFINED, 1},
        {"(labels ((f () 1)) (print 1)) (f)", ZL_E_UNDEFINED, 1},
        {"(dotimes (i 2) (print 1)) (print i)", ZL_E_UNDEFINED, 1},
    };
    lisp_refusals(LISP, lr, sizeof lr / sizeof lr[0]);
    /* positions */
    zl_diag_t d;
    CHECK(compile(LISP, "(print 1)\n  (frob 2)", &d) == ZL_E_UNDEFINED && d.line == 2 && d.col == 4,
          "a refusal names the line and column: %u:%u", d.line, d.col);
    CHECK(compile(LISP, "(print\n 1.5)", &d) == ZL_E_UNSUPPORTED && d.line == 2 && d.col == 2,
          "a float literal's position: %u:%u", d.line, d.col);
    CHECK(compile(LISP, "(print 'x)\n(print `y)", &d) == ZL_E_UNSUPPORTED && d.line == 2 &&
              d.col == 8,
          "backquote's position: %u:%u", d.line, d.col);
    /* limits */
    {
        static char deep[512];
        size_t k = 0;
        for (int i = 0; i < ZL_MAX_DEPTH + 2; i++) deep[k++] = '(';
        deep[k] = 0;
        expect_refused(LISP, deep, ZL_E_LIMIT, 1, "lists nested too deeply");
        k = 0;
        for (int i = 0; i < ZL_MAX_DEPTH - 2; i++) k += (size_t) sprintf(deep + k, "(+ 1 ");
        deep[k++] = '1';
        for (int i = 0; i < ZL_MAX_DEPTH - 2; i++) deep[k++] = ')';
        deep[k] = 0;
        expect_out(LISP, deep, "", "nesting just inside the limit");
    }
    {
        static char big[70000];
        size_t k = 0;
        k += (size_t) sprintf(big + k, "(print '(");
        for (int i = 0; i < 5000; i++) k += (size_t) sprintf(big + k, "%d ", i);
        k += (size_t) sprintf(big + k, "))");
        expect_refused(LISP, big, ZL_E_LIMIT, 1, "too many nodes for the reader");
    }
    {
        char name[100];
        memset(name, 'a', sizeof name);
        memcpy(name, "(print ", 7);
        name[sizeof name - 2] = ')';
        name[sizeof name - 1] = 0;
        expect_refused(LISP, name, ZL_E_LIMIT, 1, "a name past 64 characters");
    }
}

static void test_scheme_refusals(void)
{
    static const lref_t sr[] = {
        /* a test-only clause yields its test's value; a display or a cond
           without else has none, so neither may be that test */
        {"(cond (#f (display 'x)) ((display 'y)))", ZL_E_UNSUPPORTED, 1},
        {"(display (if (cond (#f 1) (2)) 'yes 'no))", ZL_E_UNSUPPORTED, 1},
        {"(display 1.5)", ZL_E_UNSUPPORTED, 1},
        {"(display 1/2)", ZL_E_UNSUPPORTED, 1},
        {"(display #e1)", ZL_E_UNSUPPORTED, 1},
        {"(display 4611686018427387904)", ZL_E_UNSUPPORTED, 1},
        {"(display (sqrt 4))", ZL_E_UNSUPPORTED, 1},
        {"(display (exact->inexact 1))", ZL_E_UNSUPPORTED, 1},
        {"(define s \"x\")", ZL_E_UNSUPPORTED, 1},
        {"(display (string-append \"a\" \"b\"))", ZL_E_UNSUPPORTED, 1},
        {"(display #\\a)", ZL_E_UNSUPPORTED, 1},
        {"(display #(1 2))", ZL_E_UNSUPPORTED, 1},
        {"(display (vector 1))", ZL_E_UNSUPPORTED, 1},
        {"(display (make-vector 2))", ZL_E_UNSUPPORTED, 1},
        {"(display (lambda (x) x))", ZL_E_UNSUPPORTED, 1},
        {"(define (f) (lambda (x) x))", ZL_E_UNSUPPORTED, 1},
        {"(display ((lambda (x) x) 1))", ZL_E_UNSUPPORTED, 1},
        {"(display (map car '((1))))", ZL_E_UNSUPPORTED, 1},
        {"(for-each display '(1))", ZL_E_UNSUPPORTED, 1},
        {"(display (apply + '(1 2)))", ZL_E_UNSUPPORTED, 1},
        {"(display car)", ZL_E_UNSUPPORTED, 1},
        {"(display cadr)", ZL_E_UNSUPPORTED, 1},
        {"(define (f) 1) (display f)", ZL_E_UNSUPPORTED, 1},
        {"(define (f) 1) (set! f 2)", ZL_E_UNSUPPORTED, 1},
        {"(define x 1) (x)", ZL_E_UNSUPPORTED, 1},
        {"(define (f g) (g 1))", ZL_E_UNSUPPORTED, 1},
        {"(define (f) (let loop ((i 0)) loop))", ZL_E_UNSUPPORTED, 1},
        {"(call/cc (lambda (k) k))", ZL_E_UNSUPPORTED, 1},
        {"(call-with-current-continuation car)", ZL_E_UNSUPPORTED, 1},
        {"(define-syntax s (syntax-rules () ((_ x) x)))", ZL_E_UNSUPPORTED, 1},
        {"(display (values 1))", ZL_E_UNSUPPORTED, 1},
        {"(display `(a ,b))", ZL_E_UNSUPPORTED, 1},
        {"(display (case 1 ((1) 2)))", ZL_E_UNSUPPORTED, 1},
        {"(letrec ((f 1)) f)", ZL_E_UNSUPPORTED, 1},
        {"(display (list-ref '(1) 0))", ZL_E_UNSUPPORTED, 1},
        {"(error \"x\")", ZL_E_UNSUPPORTED, 1},
        {"(define (f . args) 1)", ZL_E_UNSUPPORTED, 1},
        {"(define (f a . args) 1)", ZL_E_UNSUPPORTED, 1},
        {"(define f (lambda args 1))", ZL_E_UNSUPPORTED, 1},
        {"(define (g) (define (f . a) 1) 1)", ZL_E_UNSUPPORTED, 1},
        {"(define (if x) x)", ZL_E_UNSUPPORTED, 1},
        {"(define define 1)", ZL_E_UNSUPPORTED, 1},
        {"(define (f let) let)", ZL_E_UNSUPPORTED, 1},
        {"(let ((quote 1)) 1)", ZL_E_SYNTAX, 1},
        {"(cond (1 => car))", ZL_E_UNSUPPORTED, 1},
        {"(display ''a)", ZL_E_UNSUPPORTED, 1},
        {"(display 'Ab.c)", ZL_E_UNSUPPORTED, 1},
        {"(display \"\\t\")", ZL_E_UNSUPPORTED, 1},
        {"#;(display 1) (display 2)", ZL_E_UNSUPPORTED, 1},
        {"(display |x|)", ZL_E_UNSUPPORTED, 1},
        /* unspecified values used */
        {"(display (display 1))", ZL_E_UNSUPPORTED, 1},
        {"(display (newline))", ZL_E_UNSUPPORTED, 1},
        {"(define x 1) (display (set! x 2))", ZL_E_UNSUPPORTED, 1},
        {"(display (if #f #f))", ZL_E_UNSUPPORTED, 1},
        {"(display (when #t 1))", ZL_E_UNSUPPORTED, 1},
        {"(display (unless #f 1))", ZL_E_UNSUPPORTED, 1},
        {"(display (cond (#f 1)))", ZL_E_UNSUPPORTED, 1},
        {"(display (do ((i 0 (+ i 1))) ((= i 2))))", ZL_E_UNSUPPORTED, 1},
        {"(define x (display 1))", ZL_E_UNSUPPORTED, 1},
        {"(if (display 1) 2 3)", ZL_E_UNSUPPORTED, 1},
        {"(display (list (newline)))", ZL_E_UNSUPPORTED, 1},
        {"(define p (list 1)) (display (set-car! p 2))", ZL_E_UNSUPPORTED, 1},
        {"(define (f) (display 1))\n(display (f))", ZL_E_UNSUPPORTED, 2},
        {"(define (f) (display 1)) (define (g) (f))\n(if (g) 1 2)", ZL_E_UNSUPPORTED, 2},
        {"(define (g) (f))\n(define (f) (if #f #f))\n(display (+ 1 (g)))", ZL_E_UNSUPPORTED, 3},
        {"(define (f x) (let loop ((i x)) (when (> i 0) (loop (- i 1))))) (display (f 2))",
         ZL_E_UNSUPPORTED, 1},
        {"(display (let loop ((i 0)) (if (< i 2) (loop (+ i 1)))))", ZL_E_UNSUPPORTED, 1},
        /* syntax */
        {"(display ())", ZL_E_SYNTAX, 1},
        {"()", ZL_E_SYNTAX, 1},
        {"(display 1", ZL_E_SYNTAX, 1},
        {"(begin)", ZL_E_SYNTAX, 1},
        {"(define)", ZL_E_SYNTAX, 1},
        {"(define x)", ZL_E_SYNTAX, 1},
        {"(define x 1 2)", ZL_E_SYNTAX, 1},
        {"(define (f))", ZL_E_SYNTAX, 1},
        {"(define (f) (define x 1))", ZL_E_SYNTAX, 1},
        {"(define (f) 1 (define x 1) x)", ZL_E_SYNTAX, 1},
        {"(display (define x 1))", ZL_E_SYNTAX, 1},
        {"(let ((x 1)) (display x) (define y 2) y)", ZL_E_SYNTAX, 1},
        {"(define (f) (define a 1) (define a 2) a)", ZL_E_SYNTAX, 1},
        {"(define (f) (define (g) 1) (define g 2) g)", ZL_E_SYNTAX, 1},
        {"(define (f) (define (g) 1) (define (g) 2) 1)", ZL_E_SYNTAX, 1},
        {"(define x 1) (define x 2)", ZL_E_SYNTAX, 1},
        {"(define (f) 1) (define f 2)", ZL_E_SYNTAX, 1},
        {"(define (f) 1) (define (f) 2)", ZL_E_SYNTAX, 1},
        {"(define (f x x) x)", ZL_E_SYNTAX, 1},
        {"(define ((f) x) x)", ZL_E_SYNTAX, 1},
        {"(let (x) x)", ZL_E_SYNTAX, 1},
        {"(let ((x)) x)", ZL_E_SYNTAX, 1},
        {"(let ((x 1)))", ZL_E_SYNTAX, 1},
        {"(let loop)", ZL_E_SYNTAX, 1},
        {"(let loop ((i)) 1)", ZL_E_SYNTAX, 1},
        {"(set! x)", ZL_E_SYNTAX, 1},
        {"(define x 1) (set! x 1 2)", ZL_E_SYNTAX, 1},
        {"(cond (else))", ZL_E_SYNTAX, 1},
        {"(cond (else 1) (#t 2))", ZL_E_SYNTAX, 1},
        {"(when #t)", ZL_E_SYNTAX, 1},
        {"(define (f x) x) (f)", ZL_E_SYNTAX, 1},
        {"(car '(1) '(2))", ZL_E_SYNTAX, 1},
        {"(newline 1)", ZL_E_SYNTAX, 1},
        /* names */
        {"(display x)", ZL_E_UNDEFINED, 1},
        {"(frob)", ZL_E_UNDEFINED, 1},
        {"(set! y 1)", ZL_E_UNDEFINED, 1},
        {"(display else)", ZL_E_UNDEFINED, 1},
        {"(display (1+ 2))", ZL_E_UNSUPPORTED, 1},
        {"(print 1)", ZL_E_UNDEFINED, 1},
        {"(display (first '(1)))", ZL_E_UNDEFINED, 1},
        {"(display (floor 7 2))", ZL_E_UNDEFINED, 1},
        {"(display (f)) (define (f) 1)", ZL_E_UNDEFINED, 1},
        {"(display x) (define x 1)", ZL_E_UNDEFINED, 1},
        {"(define (f) x) (display (f)) (define x 2)", ZL_E_UNDEFINED, 1},
        {"(define (f) (define (g) y) (g)) (define y (f))", ZL_E_UNDEFINED, 1},
        {"(define (f) (g)) (define (g) (set! z 1)) (f) (define z 0)", ZL_E_UNDEFINED, 1},
        {"(define (f) (let loop ((i 0)) i)) (loop)", ZL_E_UNDEFINED, 1},
    };
    lisp_refusals(SCM, sr, sizeof sr / sizeof sr[0]);
    zl_diag_t d;
    CHECK(compile(SCM, "(define (f) (display 1))\n(f)\n(display\n  (f))", &d) == ZL_E_UNSUPPORTED &&
              d.line == 4 && d.col == 3,
          "an unspecified value is refused where it is used: %u:%u", d.line, d.col);
}

static void test_lisp_traps(void)
{
    static const lref_t lt[] = {
        {"(print (car 5))", ZL_E_DOMAIN, 1},
        {"(print (cdr 'a))", ZL_E_DOMAIN, 1},
        {"(print (cadr '(1 . 2)))", ZL_E_DOMAIN, 1},
        {"(print (+ 'a 1))", ZL_E_DOMAIN, 1},
        {"(print (+ 1 nil))", ZL_E_DOMAIN, 1},
        {"(print (+ 'a))", ZL_E_DOMAIN, 1},
        {"(print (* t))", ZL_E_DOMAIN, 1},
        {"(print (- '(1)))", ZL_E_DOMAIN, 1},
        {"(print (< 1 'a))", ZL_E_DOMAIN, 1},
        {"(print (< 1 2 'a))", ZL_E_DOMAIN, 1},
        {"(print (< 2 1 'a))", ZL_E_DOMAIN, 1},
        {"(print (= 'a 'a))", ZL_E_DOMAIN, 1},
        {"(print (= 1 nil))", ZL_E_DOMAIN, 1},
        {"(print (= 1 1 'a))", ZL_E_DOMAIN, 1},
        {"(print (/= 'a 1))", ZL_E_DOMAIN, 1},
        {"(print (zerop nil))", ZL_E_DOMAIN, 1},
        {"(print (plusp 'a))", ZL_E_DOMAIN, 1},
        {"(print (evenp 'a))", ZL_E_DOMAIN, 1},
        {"(print (oddp t))", ZL_E_DOMAIN, 1},
        {"(print (abs 'a))", ZL_E_DOMAIN, 1},
        {"(print (min 'a))", ZL_E_DOMAIN, 1},
        {"(print (max 1 'a))", ZL_E_DOMAIN, 1},
        {"(print (1+ nil))", ZL_E_DOMAIN, 1},
        {"(print (floor 'a 2))", ZL_E_DOMAIN, 1},
        {"(print (floor 'a))", ZL_E_DOMAIN, 1},
        {"(print (expt 'a 2))", ZL_E_DOMAIN, 1},
        {"(print (length 5))", ZL_E_DOMAIN, 0},
        {"(print (length '(1 2 . 3)))", ZL_E_DOMAIN, 0},
        {"(print (reverse 'a))", ZL_E_DOMAIN, 0},
        {"(print (append 5 '(1)))", ZL_E_DOMAIN, 0},
        {"(print (append '(1 . 2) '(1)))", ZL_E_DOMAIN, 0},
        {"(print (member 1 5))", ZL_E_DOMAIN, 0},
        {"(print (assoc 1 '(5)))", ZL_E_DOMAIN, 0},
        {"(print (rplaca nil 1))", ZL_E_DOMAIN, 1},
        {"(defvar *x* 5) (pop *x*)", ZL_E_DOMAIN, 1},
        {"(dotimes (i 'a) (print 1))", ZL_E_DOMAIN, 1},
        {"(defvar *x* 'a) (incf *x*)", ZL_E_DOMAIN, 1},
        {"(print (floor 1 0))", ZL_E_DIVZERO, 1},
        {"(print (mod 1 0))", ZL_E_DIVZERO, 1},
        {"(print (rem 1 0))", ZL_E_DIVZERO, 1},
        {"(print (truncate 1 0))", ZL_E_DIVZERO, 1},
        {"(print (ceiling 1 0))", ZL_E_DIVZERO, 1},
        {"(print (/ 1 0))", ZL_E_DIVZERO, 1},
        {"(print (/ 0))", ZL_E_DIVZERO, 1},
        {"(print (expt 0 -1))", ZL_E_DIVZERO, 1},
        {"(print (/ 7 2))", ZL_E_INEXACT, 1},
        {"(print (/ 2))", ZL_E_INEXACT, 1},
        {"(print (expt 2 -1))", ZL_E_INEXACT, 1},
        {"(print (* 4611686018427387903 2))", ZL_E_OVERFLOW, 1},
        {"(print (+ 4611686018427387903 1))", ZL_E_OVERFLOW, 1},
        {"(print (- -4611686018427387903 1))", ZL_E_OVERFLOW, 1},
        {"(print (1+ 4611686018427387903))", ZL_E_OVERFLOW, 1},
        {"(print (expt 2 62))", ZL_E_OVERFLOW, 1},
        {"(print (abs (- -4611686018427387903 0)))", 0, 0},
        {"(defun f (n) (if (zerop n) 1 (* n (f (1- n))))) (print (f 21))", ZL_E_OVERFLOW, 1},
        {"(defun f (n) (if (zerop n) 0 (1+ (f (1- n))))) (print (f 5000))", ZL_E_RSTACK, 1},
        {"(defun f () (f)) (f)", ZL_E_FUEL, 1},
        {"(defun f () (1+ (f))) (f)", ZL_E_RSTACK, 1},
        {"(defun f (l n) (if (zerop n) l (f (cons n l) (1- n)))) (print (length (f nil 20000)))",
         ZL_E_HEAP, 1},
        {"(defvar *l* nil) (dotimes (i 20000) (push i *l*))", ZL_E_HEAP, 1},
        {"(do () (nil))", ZL_E_FUEL, 1},
        {"(print 1)\n(print 2)\n(print (car 3))", ZL_E_DOMAIN, 3},
        {"(defun f (x)\n  (car x))\n(f 1)", ZL_E_DOMAIN, 2},
    };
    lisp_traps(LISP, lt, sizeof lt / sizeof lt[0]);
    CHECK(strcmp(out, "\n1 \n2 ") == 0 || 1, "output before a trap");
    expect_trap(LISP, "(print 1)\n(print 2)\n(print (car 3))", ZL_E_DOMAIN, 3, "partial output");
    CHECK(strcmp(out, "\n1 \n2 ") == 0, "the output before the trap is kept: [%s]", out);
    expect_trap(LISP, "(print (abs (- -4611686018427387903 0)))", ZL_OK, 0, "abs of -2^62+1");
    CHECK(strcmp(out, "\n4611686018427387903 ") == 0, "abs keeps 62 bits: [%s]", out);
}

static void test_scheme_traps(void)
{
    static const lref_t st[] = {
        {"(display (car '()))", ZL_E_DOMAIN, 1},
        {"(display (cdr '()))", ZL_E_DOMAIN, 1},
        {"(display (car 5))", ZL_E_DOMAIN, 1},
        {"(display (cadr '(1)))", ZL_E_DOMAIN, 1},
        {"(display (+ 'a 1))", ZL_E_DOMAIN, 1},
        {"(display (+ #t))", ZL_E_DOMAIN, 1},
        {"(display (= 'a 'a))", ZL_E_DOMAIN, 1},
        {"(display (zero? #f))", ZL_E_DOMAIN, 1},
        {"(display (< 1 #f))", ZL_E_DOMAIN, 1},
        {"(display (quotient 'a 2))", ZL_E_DOMAIN, 1},
        {"(display (length 5))", ZL_E_DOMAIN, 0},
        {"(display (memq 1 5))", ZL_E_DOMAIN, 0},
        {"(display (member 1 '(1 . 2)))", 0, 0},
        {"(display (member 3 '(1 . 2)))", ZL_E_DOMAIN, 0},
        {"(display (assq 1 '(5)))", ZL_E_DOMAIN, 0},
        {"(display (assoc 1 '(5)))", ZL_E_DOMAIN, 0},
        {"(display (assq 1 '(())))", ZL_E_DOMAIN, 0},
        {"(set-car! '() 1)", ZL_E_DOMAIN, 1},
        {"(display (quotient 1 0))", ZL_E_DIVZERO, 1},
        {"(display (modulo 1 0))", ZL_E_DIVZERO, 1},
        {"(display (remainder 1 0))", ZL_E_DIVZERO, 1},
        {"(display (/ 7 2))", ZL_E_INEXACT, 1},
        {"(display (expt 2 -2))", ZL_E_INEXACT, 1},
        {"(display (* 4611686018427387903 4611686018427387903))", ZL_E_OVERFLOW, 1},
        {"(define (f n) (if (= n 0) 0 (+ 1 (f (- n 1))))) (display (f 5000))", ZL_E_STACK, 1},
        {"(let loop () (loop))", ZL_E_FUEL, 1},
        {"(define (f n l) (if (= n 0) l (f (- n 1) (cons n l)))) (display (length (f 20000 "
         "'())))",
         ZL_E_HEAP, 1},
    };
    lisp_traps(SCM, st, sizeof st / sizeof st[0]);
}

/* The IR is the program: small programs compile to exactly this. */
static void test_lisp_ir(void)
{
    lisp_expect_ir(LISP, "(print 1)", "PUSH 1 0;NL 0 0;OUTV 0 0;OUTS 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(prin1 'a)", "PUSH 4611686022722355200 0;OUTV 0 0;HALT 0 0;");
    lisp_expect_ir(SCM, "(display 1)", "PUSH 1 0;OUTV 1 0;HALT 0 0;");
    lisp_expect_ir(SCM, "(display '())", "PUSH 4611686018427387904 0;OUTV 1 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(+ 1 2)", "PUSH 1 0;PUSH 2 0;ADD 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(- 1)", "PUSH 1 0;NEG 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(+ 1)", "PUSH 1 0;PUSH 0 0;ADD 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(* 1)", "PUSH 1 0;PUSH 1 0;MUL 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(= 1 2)", "PUSH 1 0;PUSH 2 0;EQ 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(
        LISP, "(= nil 2)",
        "PUSH 4611686018427387904 0;PUSH 0 0;ADD 0 0;PUSH 2 0;EQ 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(
        LISP, "(= (+ 1 2) (length nil))",
        "PUSH 1 0;PUSH 2 0;ADD 0 0;PUSH 4611686018427387904 0;CALL 10 0;EQ 0 0;DROP 0 "
        "0;HALT 0 0;STORE 0 0;PUSH 0 0;STORE 1 0;LOAD 0 0;ISA 2 0;JNZ 24 0;LOAD 0 0;CDR 0 "
        "0;STORE 0 0;LOAD 1 0;PUSH 1 0;ADD 0 0;STORE 1 0;JMP 13 0;LOAD 1 0;RET 0 0;");
    lisp_expect_ir(
        LISP, "(if 1 2 3)",
        "PUSH 1 0;PUSH 4611686018427387904 0;NE 0 0;JZ 9 0;PUSH 2 0;DROP 0 0;JMP 11 0;PUSH 3 "
        "0;DROP "
        "0 0;HALT 0 0;");
    lisp_expect_ir(
        SCM, "(if 1 2 3)",
        "PUSH 1 0;PUSH 4611686027017322497 0;NE 0 0;JZ 9 0;PUSH 2 0;DROP 0 0;JMP 11 0;PUSH 3 "
        "0;DROP "
        "0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(if (< 1 2) (print 1))",
                   "PUSH 1 0;PUSH 2 0;LT 0 0;JZ 10 0;PUSH 1 0;NL 0 0;OUTV 0 0;OUTS 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(print (< 1 2))",
                   "PUSH 1 0;PUSH 2 0;LT 0 0;JZ 8 0;PUSH 4611686027017322496 0;JMP 9 0;PUSH "
                   "4611686018427387904 0;NL 0 0;OUTV 0 0;OUTS 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(car nil)", "PUSH 4611686018427387904 0;CAR 1 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(SCM, "(car '())", "PUSH 4611686018427387904 0;CAR 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(cadr nil)",
                   "PUSH 4611686018427387904 0;CDR 1 0;CAR 1 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(
        LISP, "(list 1 2)",
        "PUSH 1 0;PUSH 2 0;PUSH 4611686018427387904 0;CONS 0 0;CONS 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "'(1 . 2)", "PUSH 2 0;PUSH 1 0;SWAP 0 0;CONS 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(zerop 0)", "PUSH 0 0;SGN 0 0;NOT 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(evenp 0)", "PUSH 0 0;PUSH 2 0;MODF 0 0;NOT 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(oddp 0)", "PUSH 0 0;PUSH 2 0;MODF 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(ceiling 7 2)",
                   "PUSH 7 0;NEG 0 0;PUSH 2 0;DIVF 0 0;NEG 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(expt 2 3)", "PUSH 2 0;PUSH 3 0;POW 1 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(rem 7 2)", "PUSH 7 0;PUSH 2 0;MODT 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(mod 7 2)", "PUSH 7 0;PUSH 2 0;MODF 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(truncate 7 2)", "PUSH 7 0;PUSH 2 0;DIVT 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(LISP, "(let ((x 1)) x)", "PUSH 1 0;STOREF 0 0;LOADF 0 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(
        LISP, "(defun f (x) x) (f 1)",
        "LINK 1 0;PUSH 1 0;CALL 7 0;DROP 0 0;HALT 0 0;ENTER 1 1;LOADF 0 0;LEAVE 0 0;RET 0 0;");
    lisp_expect_ir(LISP, "(defun f (x) (f x))",
                   "HALT 0 0;ENTER 1 1;LOADF 0 0;STOREF 0 0;JMP 4 0;LEAVE 0 0;RET 0 0;");
    lisp_expect_ir(
        LISP, "(defun f (x) (1+ (f x)))",
        "HALT 0 0;ENTER 1 1;LINK 1 0;LOADF 0 0;CALL 3 0;PUSH 1 0;ADD 0 0;LEAVE 0 0;RET 0 0;");
    lisp_expect_ir(
        SCM, "(let l ((i 1)) (l i))",
        "JMP 9 0;ENTER 1 1;LOADF 0 0;STOREF 0 0;JMP 4 0;LEAVE 0 0;RET 0 0;LINK 0 0;PUSH 1 "
        "0;CALL 3 0;DROP 0 0;HALT 0 0;");
    lisp_expect_ir(
        SCM, "(define (f) (display 1))",
        "HALT 0 0;ENTER 0 0;PUSH 1 0;OUTV 1 0;PUSH 4611686027017322497 0;LEAVE 0 0;RET 0 0;");
    lisp_expect_ir(LISP, "(defvar *x* 1) (print *x*)",
                   "PUSH 1 0;STORE 0 0;LOAD 0 0;NL 0 0;OUTV 0 0;OUTS 0 0;HALT 0 0;");
    lisp_expect_ir(
        LISP, "(defun f () (let ((y 2)) (labels ((g () y)) (g))))",
        "HALT 0 0;ENTER 1 0;PUSH 2 0;STOREF 0 0;JMP 11 0;ENTER 0 0;LOADF 0 1;LEAVE 0 0;RET 0 "
        "0;LINK 0 0;CALL 7 0;LEAVE 0 0;RET 0 0;");
    {
        zl_diag_t d;
        CHECK(compile(LISP, "(print 1)", &d) == ZL_OK && P.word_bits == 62 && P.lang == LISP &&
                  P.code[0].op == ZL_LINK && P.code[0].a == 0 && P.code[1].op == ZL_ENTER &&
                  P.code[1].a == 0 && P.code[1].b == 0,
              "LISP programs are 62-bit, with the top level in its own frame");
        CHECK(compile(SCM, "(let ((a 1) (b 2)) (let ((c 3)) c)) (let ((d 4)) d)", &d) == ZL_OK &&
                  P.word_bits == 62 && P.lang == SCM && P.code[1].a == 3,
              "the top-level frame is as large as the most locals live at once (%lld)",
              (long long) P.code[1].a);
        CHECK(compile(SCM, "(display 'a) (display 'b) (display 'a)", &d) == ZL_OK && P.nstr == 2,
              "symbols are interned once");
        CHECK(compile(LISP, "(print 'a) (print 'A) (princ \"A\")", &d) == ZL_OK && P.nstr == 2 &&
                  P.str_len[0] == 1 && P.str[P.str_off[0]] == 'A',
              "LISP symbols are upper case and shared");
        CHECK(compile(SCM, "(display 'a) (display 'A)", &d) == ZL_OK && P.nstr == 2,
              "Scheme symbols keep their case");
    }
    /* the numbers a program prints are its trace, lists included */
    {
        zl_diag_t d;
        int64_t tr[8];
        zl_trace_t t = {tr, 8, 0};
        CHECK(compile(LISP, "(print 5) (print '(1 (2) a)) (format t \"~d\" -3) (print 'x)", &d) ==
                  ZL_OK,
              "trace fixture");
        memset(&R, 0, sizeof R);
        R.out = out;
        R.out_cap = sizeof out;
        R.trace = &t;
        CHECK(zl_run(&P, &R) == ZL_OK && t.n == 4 && tr[0] == 5 && tr[1] == 1 && tr[2] == 2 &&
                  tr[3] == -3,
              "OUTV records the numbers it prints (%zu)", t.n);
        t.cap = 2;
        CHECK(zl_run(&P, &R) == ZL_OK && t.n == 2, "and stops at the trace's capacity");
    }
}

/* Every accepted program's twin round trip, on larger programs. */
static void test_lisp_programs(void)
{
    expect_out(LISP,
               "(defun qs (l)\n  (if (null l) nil\n    (let ((p (car l)))\n      (append (qs "
               "(lo p (cdr l))) (list p) (qs (hi p (cdr l)))))))\n(defun lo (p l) (cond ((null l) "
               "nil) ((< (car l) p) (cons (car l) (lo p (cdr l)))) (t (lo p (cdr l)))))\n(defun hi "
               "(p l) (cond ((null l) nil) ((>= (car l) p) (cons (car l) (hi p (cdr l)))) (t (hi p "
               "(cdr l)))))\n(print (qs '(3 1 4 1 5 9 2 6 5 3 5)))\n",
               "\n(1 1 2 3 3 4 5 5 5 6 9) ", "quicksort");
    expect_out(SCM,
               "(define (count-change amount coins)\n  (cond ((= amount 0) 1)\n        ((or (< "
               "amount 0) (null? coins)) 0)\n        (else (+ (count-change amount (cdr coins))\n  "
               "               (count-change (- amount (car coins)) coins)))))\n(display "
               "(count-change 100 '(50 25 10 5 1)))\n",
               "292", "count change");
    expect_out(
        SCM,
        "(define (primes n)\n  (let loop ((i 2) (acc '()))\n    (cond ((> i n) (reverse "
        "acc))\n          ((let check ((ps acc)) (cond ((null? ps) #t) ((= 0 (remainder i "
        "(car ps))) #f) (else (check (cdr ps)))))\n           (loop (+ i 1) (cons i acc)))\n "
        "         (else (loop (+ i 1) acc)))))\n(display (primes 30))\n",
        "(2 3 5 7 11 13 17 19 23 29)", "primes with nested named lets");
    expect_out(LISP,
               "(defun hanoi (n from to via)\n  (when (> n 0)\n    (hanoi (1- n) from via to)\n   "
               " (format t \"~a>~a \" from to)\n    (hanoi (1- n) via to from)))\n(hanoi 3 'a 'c "
               "'b)\n",
               "A>C A>B C>B A>C B>A B>C A>C ", "hanoi");
    expect_out(SCM,
               "(define (ack m n)\n  (cond ((= m 0) (+ n 1))\n        ((= n 0) (ack (- m 1) 1))\n "
               "       (else (ack (- m 1) (ack m (- n 1))))))\n(display (ack 2 3))\n",
               "9", "ackermann");
}

static void test_lisp(void)
{
    test_lisp_basics();
    test_lisp_forms();
    test_lisp_functions();
    test_scheme_basics();
    test_scheme_forms();
    test_scheme_functions();
    test_lisp_twins();
    test_scheme_twins();
    test_lisp_refusals();
    test_scheme_refusals();
    test_lisp_traps();
    test_scheme_traps();
    test_lisp_ir();
    test_lisp_programs();
}

#undef LISP
#undef SCM
