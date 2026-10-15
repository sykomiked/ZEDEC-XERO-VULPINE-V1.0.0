% Search: the eight queens by generate and test with backtracking,
% Ackermann, the towers of Hanoi, and a small family database.
:- initialization(main).

queens(N, Qs) :-
    numlist(1, N, Ns),
    place(Ns, [], Qs).

place([], Qs, Qs).
place(Unplaced, Safe, Qs) :-
    select1(Q, Unplaced, Rest),
    \+ attacks(Q, Safe, 1),
    place(Rest, [Q|Safe], Qs).

select1(X, [X|T], T).
select1(X, [H|T], [H|R]) :- select1(X, T, R).

attacks(Q, [Q1|_], D) :- Q =:= Q1 + D.
attacks(Q, [Q1|_], D) :- Q =:= Q1 - D.
attacks(Q, [_|Qs], D) :- D1 is D + 1, attacks(Q, Qs, D1).

ack(0, N, R) :- !, R is N + 1.
ack(M, 0, R) :- !, M1 is M - 1, ack(M1, 1, R).
ack(M, N, R) :- M1 is M - 1, N1 is N - 1, ack(M, N1, R1), ack(M1, R1, R).

hanoi(0, _, _, _) :- !.
hanoi(N, From, To, Via) :-
    M is N - 1,
    hanoi(M, From, Via, To),
    format("~a->~a ", [From, To]),
    hanoi(M, Via, To, From).

parent(tom, bob).
parent(tom, liz).
parent(bob, ann).
parent(bob, pat).
parent(pat, jim).

ancestor(X, Y) :- parent(X, Y).
ancestor(X, Y) :- parent(X, Z), ancestor(Z, Y).

main :-
    queens(8, Qs), !,
    print(Qs), nl,
    findall(Q, queens(6, Q), All),
    length(All, NAll),
    format("6 queens: ~d solutions~n", [NAll]),
    ack(2, 3, A),
    format("ack(2, 3) = ~d~n", [A]),
    hanoi(3, left, right, middle), nl,
    findall(D, ancestor(tom, D), Ds),
    write(Ds), nl,
    ( ancestor(liz, _) -> write(liz_has_descendants) ; write(liz_has_none) ), nl,
    forall(parent(P, C), (write(P - C), write(' '))), nl,
    X is 2 ** 61 - 1 + 2 ** 61,
    write(X), nl,
    Y is -(17) // 5 + (-17) mod 5 * 100 + 17 rem -5 * 10000,
    write(Y), nl,
    Z is max(3, 7) * min(-2, 4) + abs(-9) + sign(-4) + (6 /\ 3) + (1 << 4) + (5 xor 3),
    write(Z), nl.
