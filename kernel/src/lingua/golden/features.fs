\ primes and friends in Forth
: prime? ( n -- f )  dup 2 < if drop false exit then
  true swap dup 2 do dup i mod 0= if nip false swap leave then loop drop ;
: primes ( n -- ) 2 do i prime? if i . then loop ;
50 primes cr
: sq dup * ;
: sumsq 0 11 1 do i sq + loop ;
sumsq . cr
variable acc  5 acc !  7 acc +!  acc ? cr
10 constant ten  ten ten * . cr
create tbl 10 cells allot
: fill-tbl 10 0 do i i * tbl i cells + ! loop ;
fill-tbl tbl 9 cells + @ . tbl 3 cells + @ . cr
: fact ( n -- n! ) dup 1 > if dup 1- recurse * then ;
10 fact . cr
: countdown begin dup . 1- dup 0= until drop ;
5 countdown cr
: w begin dup 0 > while dup . 2 - repeat drop ;
9 w cr
-7 2 / . -7 2 mod . 7 -2 /mod . . -7 2/ . cr
1 2 = . 3 3 = . 1 2 < . -5 abs . 3 9 min . 3 9 max . cr
." hello world" 3 spaces 42 emit cr
: x 10 0 do i . 3 +loop ; x cr
: y 0 10 do i . -3 +loop ; y cr
: nest 3 0 do 2 0 do j 10 * i + . loop loop ; nest cr
5 ?dup . . 0 ?dup . cr
