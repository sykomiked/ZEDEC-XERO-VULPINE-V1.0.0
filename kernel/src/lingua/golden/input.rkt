#lang racket/base
;;; Input: a count, that many integers and then two more, one per line.
;;; Prints the integers' sum, largest and list, then the gcd of the two.
(define (read-ints n)
  (if (= n 0) '() (let ([x (read)]) (cons x (read-ints (sub1 n))))))

(define (total l)
  (if (null? l) 0 (+ (car l) (total (cdr l)))))

(define (largest l)
  (if (null? (cdr l)) (car l) (max (car l) (largest (cdr l)))))

(define (gcd2 a b)
  (if (= b 0) (abs a) (gcd2 b (modulo a b))))

(define xs (read-ints (read)))
(total xs)
(printf "largest ~a~n" (largest xs))
xs
(printf "gcd ~a~n" (gcd2 (read) (read)))
