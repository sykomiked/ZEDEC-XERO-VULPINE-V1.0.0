;;; Input: a count, that many integers and then two more, one per line.
;;; Prints the integers' sum, largest and list, the sum's remainder by 3
;;; as a word, then the gcd of the two.
(defun read-ints (n)
  (if (zerop n) nil (let ((x (read))) (cons x (read-ints (1- n))))))

(defun total (l)
  (if (null l) 0 (+ (first l) (total (rest l)))))

(defun largest (l)
  (if (null (rest l)) (first l) (max (first l) (largest (rest l)))))

(defun gcd2 (a b)
  (if (zerop b) (abs a) (gcd2 b (mod a b))))

(defvar *xs* (read-ints (read)))
(format t "sum ~d~%" (total *xs*))
(format t "largest ~d~%" (largest *xs*))
(print *xs*)
(terpri)
(format t "~a~%" (case (mod (total *xs*) 3) (0 'zero) (1 'one) (otherwise 'two)))
(format t "gcd ~d~%" (gcd2 (read) (read)))
