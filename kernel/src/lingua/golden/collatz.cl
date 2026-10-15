;;; The same three algorithms as collatz.f, collatz.bas and collatz.fs,
;;; in Common Lisp
(defun steps (n)
  (do ((k n (case (mod k 2)
              (0 (floor k 2))
              (otherwise (+ (* 3 k) 1))))
       (s 0 (1+ s)))
      ((= k 1) s)))

(defun euclid (a b)
  (if (zerop b) a (euclid b (mod a b))))

(dotimes (i 12)
  (format t "~d ~d~%" (1+ i) (steps (1+ i))))
(format t "GCD ~d~%" (euclid 1071 462))

(let ((f 0) (g 1))
  (dotimes (i 15)
    (format t "~d~%" f)
    (let ((next (+ f g)))
      (setf f g)
      (setf g next))))
