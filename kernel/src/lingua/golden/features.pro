% Control and output: cut, if-then-else, negation, disjunction, term
% comparison, type tests, and SWI's writer with operators and quoting.
:- initialization(main).
:- initialization(second).

classify(N, negative) :- N < 0, !.
classify(0, zero) :- !.
classify(N, small) :- N < 10, !.
classify(_, large).

color(red). color(green). color(blue).

first_color(C) :- color(C), !.

show(T) :- write(T), write('  '), print(T), write('  '), writeq(T), nl.

main :-
    forall(member(N, [-5, 0, 7, 12]), (classify(N, C), format("~d is ~a~n", [N, C]))),
    first_color(F), format("first ~w~n", [F]),
    findall(C, color(C), Cs), format("all ~w~n", [Cs]),
    ( \+ color(pink) -> write('no pink') ; write(pink) ), nl,
    ( color(X), X \= red -> format("not red: ~w~n", [X]) ; true ),
    ( fail ; write(either) ), nl,
    ( f(a, B) = f(A, b) -> format("~w ~w~n", [A, B]) ; true ),
    ( f(a) == f(a) -> write(same) ; write(different) ), nl,
    ( g(_) \== g(_) -> write(distinct) ; write(identical) ), nl,
    show(1 + 2 * 3),
    show((1 + 2) * 3),
    show(- (1)),
    show(-(-(a))),
    show(a - (b - c)),
    show(f('Hello', [], 'it''s', '', 'a\nb')),
    show([a, 'B'|c]),
    show({x, y}),
    show((a :- b, c ; d -> e)),
    show(\+ a),
    show(1 - -1),
    show(2 ** -1),
    show(f(;, '|', '', -)),
    format("~a and ~w~n~q~n", [abc, 'x y', 'x y']),
    format("~c~c~i~w~n", [104, 105, skipped, '!']),
    format("tilde ~~ done~n"),
    tab(3), write(indented), nl,
    writeln(end).

second :- write(second), nl, X is 7 - 10, Y is X * X, format("~d ~d~n", [X, Y]).
