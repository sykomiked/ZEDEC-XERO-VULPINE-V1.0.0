;;; The same three algorithms as collatz.f, collatz.bas and collatz.fs
(define (steps n)
  (let loop ((k n) (s 0))
    (cond ((= k 1) s)
          ((even? k) (loop (quotient k 2) (+ s 1)))
          (else (loop (+ (* 3 k) 1) (+ s 1))))))

(define (euclid a b)
  (if (= b 0) a (euclid b (modulo a b))))

(do ((n 1 (+ n 1))) ((> n 12))
  (display n) (display " ") (display (steps n)) (newline))
(display "GCD ") (display (euclid 1071 462)) (newline)

(let fib ((i 0) (f 0) (g 1))
  (when (< i 15)
    (display f)
    (newline)
    (fib (+ i 1) g (+ f g))))
