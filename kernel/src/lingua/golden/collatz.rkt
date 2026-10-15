#lang racket
;;; The same three algorithms as collatz.f, collatz.bas and collatz.fs
(define (steps n)
  (let loop ([k n] [s 0])
    (cond [(= k 1) s]
          [(even? k) (loop (quotient k 2) (add1 s))]
          [else (loop (+ (* 3 k) 1) (add1 s))])))

(define (euclid a b)
  (if (zero? b) a (euclid b (modulo a b))))

(define (table n)
  (when (<= n 12)
    (printf "~a ~a~n" n (steps n))
    (table (add1 n))))

(table 1)
(printf "GCD ~a\n" (euclid 1071 462))

(let fib ([i 0] [f 0] [g 1])
  (when (< i 15)
    (displayln f)
    (fib (add1 i) g (+ f g))))
