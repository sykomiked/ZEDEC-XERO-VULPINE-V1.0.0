#lang racket
;;; Lists: reverse, append, an association list and quicksort, written
;;; out by hand and checked against the built-in procedures. A module
;;; prints the value of each top-level expression that is not void.
(define (my-reverse l)
  (let loop ([l l] [acc '()])
    (if (empty? l) acc (loop (rest l) (cons (first l) acc)))))

(define (my-append a b)
  (if (null? a) b (cons (car a) (my-append (cdr a) b))))

(define (partition p l less more)
  (cond [(empty? l) (list less more)]
        [(< (first l) p) (partition p (rest l) (cons (first l) less) more)]
        [else (partition p (rest l) less (cons (first l) more))]))

(define (qsort l)
  (if (empty? l)
      empty
      (let ([parts (partition (first l) (rest l) '() '())])
        (append (qsort (first parts)) (list (first l)) (qsort (second parts))))))

(define (bump k al)
  (cond [(null? al) (list (cons k 1))]
        [(eq? (caar al) k) (cons (cons k (add1 (cdar al))) (cdr al))]
        [else (cons (car al) (bump k (cdr al)))]))

(define (tally l al)
  (if (null? l) al (tally (cdr l) (bump (car l) al))))

(define (upto a b)
  (if (> a b) null (cons a (upto (add1 a) b))))

(define data '(31 4 15 9 26 5 35 8 97 9 32 3 84 6 26 43))
(define ages '((alice . 30) (bob . 25) (carol . 41)))

(my-reverse data)
(equal? (my-reverse data) (reverse data))
(my-append '(1 2 3) '(4 5))
(equal? (my-append (upto 1 5) (upto 6 9)) (append (upto 1 5) (upto 6 9)))
(qsort data)
(length (qsort data))
(assq 'bob ages)
(cdr (assq 'carol ages))
(assq 'dave ages)
(set! ages (cons (cons 'dave 19) ages))
(cdr (assoc 'dave ages))
(displayln ages)
(println ages)
(tally '(a b a c b a d) '())
(member 26 data)
(list (upto 1 3) (cons 1 2) (list 'nested (list 1 (list 2 '()))))
(list (memq 'c '(a b c)) (member '(2) '((1) (2) (3))) (assoc '(k) '(((k) . 5))))
(list (first data) (second data) (third data) (cons? data) (cons? '()) (empty? '()))
(printf "~a items, first ~s, sorted ~v~n" (length data) (first data) (qsort '(3 1 2)))
'sym
'()
