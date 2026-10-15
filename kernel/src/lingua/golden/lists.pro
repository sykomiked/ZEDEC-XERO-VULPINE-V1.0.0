% Lists: the library predicates, naive reverse, quicksort, permutations
% by backtracking, and findall.
:- initialization(main).

nrev([], []).
nrev([H|T], R) :- nrev(T, RT), append(RT, [H], R).

partition(_, [], [], []).
partition(P, [X|Xs], [X|L], G) :- X =< P, !, partition(P, Xs, L, G).
partition(P, [X|Xs], L, [X|G]) :- partition(P, Xs, L, G).

qsort([], []).
qsort([P|Xs], S) :-
    partition(P, Xs, L, G),
    qsort(L, SL),
    qsort(G, SG),
    append(SL, [P|SG], S).

sel(X, [X|T], T).
sel(X, [H|T], [H|R]) :- sel(X, T, R).

perm([], []).
perm(L, [X|P]) :- sel(X, L, R), perm(R, P).

squares([], []).
squares([X|Xs], [Y|Ys]) :- Y is X * X, squares(Xs, Ys).

main :-
    numlist(1, 10, L),
    nrev(L, R),
    print(R), nl,
    qsort([33, 5, 81, -2, 17, 5, 0, 64, 12], S),
    print(S), nl,
    length(L, N),
    sum_list(L, Sum),
    max_list(S, Max),
    min_list(S, Min),
    format("~d items, sum ~d, max ~d, min ~d~n", [N, Sum, Max, Min]),
    findall(P, perm([a, b, c], P), Ps),
    length(Ps, NP),
    format("~d permutations: ~w~n", [NP, Ps]),
    squares(L, Sq),
    last(Sq, Last),
    nth1(3, Sq, Third),
    format("squares ~w, third ~d, last ~d~n", [Sq, Third, Last]),
    ( member(X, Sq), X > 40 -> format("first square over 40: ~d~n", [X]) ; true ),
    ( memberchk(49, Sq) -> write(yes) ; write(no) ), nl,
    append(Front, [9, 10], L),
    write(Front), nl,
    reverse([x, y, z], RX),
    write(RX), nl,
    length(Fresh, 3),
    Fresh = [p | _],
    findall(K-V, (nth0(K, [p, q, r], V)), KV),
    print(KV), nl,
    findall(Len, (between(0, 3, Len), length(LL, Len), LL \== [x]), Ls),
    print(Ls), nl.
