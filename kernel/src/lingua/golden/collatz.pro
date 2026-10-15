% The same three algorithms as collatz.f, collatz.bas and collatz.lisp,
% as Horn clauses: steps by recursion, Euclid, and Fibonacci by
% accumulators.
:- initialization(main).

steps(1, 0) :- !.
steps(N, S) :-
    N mod 2 =:= 0, !,
    M is N // 2,
    steps(M, S0),
    S is S0 + 1.
steps(N, S) :-
    M is 3 * N + 1,
    steps(M, S0),
    S is S0 + 1.

gcd(A, 0, A) :- !.
gcd(A, B, G) :- C is A mod B, gcd(B, C, G).

fib(0, _, _) :- !.
fib(K, F, G) :- format("~d~n", [F]), H is F + G, K1 is K - 1, fib(K1, G, H).

main :-
    forall(between(1, 12, N), (steps(N, S), format("~d ~d~n", [N, S]))),
    gcd(1071, 462, G),
    format("GCD ~d~n", [G]),
    fib(15, 0, 1).
