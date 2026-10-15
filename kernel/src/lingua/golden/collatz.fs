\ The same three algorithms as collatz.f and collatz.bas
: steps ( n -- s ) 0 swap begin dup 1 <> while
    dup 2 mod 0= if 2 / else 3 * 1+ then swap 1+ swap repeat drop ;
: collatz 13 1 do i . i steps . cr loop ;
: gcd ( a b -- g ) begin dup while tuck mod repeat drop ;
: fib 0 1 15 0 do over . cr tuck + loop 2drop ;
collatz
." GCD " 1071 462 gcd . cr
fib
