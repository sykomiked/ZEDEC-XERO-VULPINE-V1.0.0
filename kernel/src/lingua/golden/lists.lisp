;;; Lists: reverse, append, an association list and quicksort, written
;;; out by hand and checked against the built-in functions.
(defun my-reverse (l)
  (labels ((rev (l acc)
             (if (null l) acc (rev (cdr l) (cons (car l) acc)))))
    (rev l nil)))

(defun my-append (a b)
  (if (null a) b (cons (car a) (my-append (cdr a) b))))

(defun below (p l)
  (cond ((null l) nil)
        ((< (car l) p) (cons (car l) (below p (cdr l))))
        (t (below p (cdr l)))))

(defun not-below (p l)
  (cond ((null l) nil)
        ((>= (car l) p) (cons (car l) (not-below p (cdr l))))
        (t (not-below p (cdr l)))))

(defun qsort (l)
  (if (null l)
      nil
      (let ((p (car l)) (r (cdr l)))
        (append (qsort (below p r)) (cons p (qsort (not-below p r)))))))

(defun bump (k al)
  (cond ((null al) (list (cons k 1)))
        ((eq (caar al) k) (cons (cons k (1+ (cdar al))) (cdr al)))
        (t (cons (car al) (bump k (cdr al))))))

(defun tally (l al)
  (if (null l) al (tally (cdr l) (bump (car l) al))))

(defun range (a b)
  (if (> a b) nil (cons a (range (1+ a) b))))

(defvar *data* '(31 4 15 9 26 5 35 8 97 9 32 3 84 6 26 43))
(defvar *ages* '((alice . 30) (bob . 25) (carol . 41)))

(print (my-reverse *data*))
(print (equal (my-reverse *data*) (reverse *data*)))
(print (my-append '(1 2 3) '(4 5)))
(print (equal (my-append (range 1 5) (range 6 9)) (append (range 1 5) (range 6 9))))
(print (qsort *data*))
(print (length (qsort *data*)))
(print (assoc 'bob *ages*))
(print (cdr (assoc 'carol *ages*)))
(print (assoc 'dave *ages*))
(push (cons 'dave 19) *ages*)
(print (cdr (assoc 'dave *ages*)))
(print *ages*)
(print (tally '(a b a c b a d) nil))
(print (member 26 *data*))
(print (list (range 1 3) (cons 1 2) (list 'nested (list 1 (list 2 nil)))))
