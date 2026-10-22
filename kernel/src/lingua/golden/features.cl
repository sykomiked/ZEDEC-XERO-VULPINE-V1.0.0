;;; Common Lisp: the LISP core plus case, written in lower case with
;;; first/rest and setf, as Braidwyrm writes it
(defvar *tally* nil)
(defparameter *limit* 12)

(defun kind (x)
  (case x
    ((0) 'zero)
    ((1 3 5 7 9) 'odd-digit)
    ((2 4 6 8) 'even-digit)
    (ten 'ten-symbol)
    (otherwise 'other)))

(defun note (k)
  (let ((hit (assoc k *tally*)))
    (if hit
        (rplacd hit (1+ (rest hit)))
        (push (cons k 1) *tally*))))

(defun digits (n acc)
  (if (< n 10)
      (cons n acc)
      (digits (floor n 10) (cons (mod n 10) acc))))

(defun fib (n)
  (labels ((walk (i a b)
             (if (= i n) a (walk (1+ i) b (+ a b)))))
    (walk 0 0 1)))

(defun kinds (l)
  (if (null l) nil (cons (kind (first l)) (kinds (rest l)))))

(print (kinds '(0 1 2 ten 11 eleven 9)))
(dotimes (i *limit*)
  (note (kind (mod (* i i) 11))))
(print *tally*)
(print (digits (fib 60) nil))
(format t "~%fib 80 = ~d~%" (fib 80))
(print (case (first (digits 2026 nil))
         (1 'one)
         (2 'two)
         (t 'many)))
(print (case 'x (y 1)))
(setq *limit* (+ *limit* 1))
(setf *limit* (* *limit* 2))
(print *limit*)
(print (list (car '(a b c)) (cdr '(a b c)) (cadr '(a b c)) (caddr '(a b c)) (cddr '(a b c))))
(print (list (first '(a b c)) (second '(a b c)) (third '(a b c))))
(let ((x 5) y)
  (setf y (* x x))
  (print (list x y)))
(print (do ((i 0 (1+ i)) (acc nil (cons (kind i) acc))) ((= i 3) acc)))
(print 'done)
