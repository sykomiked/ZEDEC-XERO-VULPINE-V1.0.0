;;; Input: a count, that many integers and then two more, one per line.
;;; Prints the integers' sum, largest and list, then the gcd of the two.
(define (read-ints n)
  (if (= n 0) '() (let ((x (read))) (cons x (read-ints (- n 1))))))

(define (total l)
  (if (null? l) 0 (+ (car l) (total (cdr l)))))

(define (largest l)
  (if (null? (cdr l)) (car l) (max (car l) (largest (cdr l)))))

(define (gcd2 a b)
  (if (= b 0) (abs a) (gcd2 b (modulo a b))))

(define xs (read-ints (read)))
(display "sum ")
(display (total xs))
(newline)
(display "largest ")
(display (largest xs))
(newline)
(write xs)
(newline)
(let* ((a (read)) (b (read)))
  (display "gcd ")
  (display (gcd2 a b))
  (newline))
