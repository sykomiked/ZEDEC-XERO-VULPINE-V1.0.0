;;; Lists: reverse, append, an association list and quicksort, written
;;; out by hand and checked against the built-in procedures.
(define (my-reverse l)
  (let loop ((l l) (acc '()))
    (if (null? l) acc (loop (cdr l) (cons (car l) acc)))))

(define (my-append a b)
  (if (null? a) b (cons (car a) (my-append (cdr a) b))))

(define (partition p l less more)
  (cond ((null? l) (list less more))
        ((< (car l) p) (partition p (cdr l) (cons (car l) less) more))
        (else (partition p (cdr l) less (cons (car l) more)))))

(define (qsort l)
  (if (null? l)
      '()
      (let ((parts (partition (car l) (cdr l) '() '())))
        (append (qsort (car parts)) (list (car l)) (qsort (cadr parts))))))

(define (bump k al)
  (cond ((null? al) (list (cons k 1)))
        ((eq? (caar al) k) (cons (cons k (+ 1 (cdar al))) (cdr al)))
        (else (cons (car al) (bump k (cdr al))))))

(define (tally l al)
  (if (null? l) al (tally (cdr l) (bump (car l) al))))

(define (range a b)
  (if (> a b) '() (cons a (range (+ a 1) b))))

(define (show x)
  (display x)
  (newline))

(define data '(31 4 15 9 26 5 35 8 97 9 32 3 84 6 26 43))
(define ages '((alice . 30) (bob . 25) (carol . 41)))

(show (my-reverse data))
(show (equal? (my-reverse data) (reverse data)))
(show (my-append '(1 2 3) '(4 5)))
(show (equal? (my-append (range 1 5) (range 6 9)) (append (range 1 5) (range 6 9))))
(show (qsort data))
(show (length (qsort data)))
(show (assq 'bob ages))
(show (cdr (assq 'carol ages)))
(show (assq 'dave ages))
(set! ages (cons (cons 'dave 19) ages))
(show (cdr (assoc 'dave ages)))
(show ages)
(show (tally '(a b a c b a d) '()))
(show (member 26 data))
(show (list (range 1 3) (cons 1 2) (list 'nested (list 1 (list 2 '())))))
(show (list (memq 'c '(a b c)) (member '(2) '((1) (2) (3))) (assoc '(k) '(((k) . 5)))))
