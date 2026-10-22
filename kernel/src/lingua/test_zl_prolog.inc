/* test_zl_prolog.inc — Prolog front-end tests, included by test_zl.c.
 * Every expect_out text is SWI-Prolog 9.0.4's own output for the same
 * program (swipl -q -t halt), recorded when these tests were written. */

static void test_prolog(void)
{
    const int G = ZL_LANG_PROLOG;
    expect_out(G, ":- initialization(main).\nmain :- write(hello), nl.\n",
               "hello\n", "write an atom");
    expect_out(G, ":- initialization(main).\nmain :- write(42), nl, write(-7), nl.\n",
               "42\n-7\n", "write integers");
    expect_out(G, ":- initialization(main).\nmain :- X = 4611686018427387903, write(X), nl, Y is -X, write(Y), nl.\n",
               "4611686018427387903\n-4611686018427387903\n", "largest integers");
    expect_out(G, ":- initialization(main).\nmain :- print('hello world'), nl, write('hello world'), nl.\n",
               "'hello world'\nhello world\n", "print quotes, write does not");
    expect_out(G, ":- initialization(main).\nmain :- writeq(['A', b, 'it''s', 'a\\nb', [], '[]x', {}, ';', '!', ',', '|', '', 'x y']), nl.\n",
               "['A',b,'it\\'s','a\\nb',[],'[]x',{},;,!,',','|','','x y']\n", "writeq quoting");
    expect_out(G, ":- initialization(main).\nmain :- writeq(f('\\\\', 'tab\\there')), nl.\n",
               "f(\\,'tab\\there')\n", "writeq escapes");
    expect_out(G, ":- initialization(main).\nmain :- writeln(f(x, [1, 2])), writeln(abc).\n",
               "f(x,[1,2])\nabc\n", "writeln");
    expect_out(G, ":- initialization(main).\nmain :- print([1, 2, 3]), nl, print([a|b]), nl, print([[a], [b, [c]]]), nl.\n",
               "[1,2,3]\n[a|b]\n[[a],[b,[c]]]\n", "lists");
    expect_out(G, ":- initialization(main).\nmain :- print(f(a, g(b), h)), nl, print({a, b}), nl, print('{}'(x)), nl.\n",
               "f(a,g(b),h)\n{a,b}\n{x}\n", "compounds and curly terms");
    expect_out(G, ":- initialization(main).\nmain :- print(1 + 2 * 3), nl, print((1 + 2) * 3), nl, print(1 - (2 - 3)), nl, print(1 - 2 - 3), nl.\n",
               "1+2*3\n(1+2)*3\n1-(2-3)\n1-2-3\n", "operators and parentheses");
    expect_out(G, ":- initialization(main).\nmain :- print(- 1), nl, print(-(1)), nl, print(-(-1)), nl, print(- a), nl, print(-(-(a))), nl, print(1 - -1), nl.\n",
               "- 1\n- 1\n- -1\n-a\n- -a\n1- -1\n", "minus signs");
    expect_out(G, ":- initialization(main).\nmain :- print(a :- b), nl, print((a :- b, c ; d -> e)), nl, print(\\+ a), nl, print(\\+ (a, b)), nl.\n",
               "a:-b\na:-b,c;d->e\n\\+a\n\\+ (a,b)\n", "control terms");
    expect_out(G, ":- initialization(main).\nmain :- print(f((a, b))), nl, print(f((a :- b))), nl, print([(a ; b), (c -> d)]), nl, print(f(;)), nl.\n",
               "f((a,b))\nf((a:-b))\n[(a;b),(c->d)]\nf(;)\n", "arguments above 999");
    expect_out(G, ":- initialization(main).\nmain :- print(1 rem 2), nl, print(a mod b), nl, print(x is 1 + 2), nl, print(a = b), nl, print(a =.. b), nl.\n",
               "1 rem 2\na mod b\nx is 1+2\na=b\na=..b\n", "alphanumeric and symbolic operators");
    expect_out(G, ":- initialization(main).\nmain :- print(- (-)), nl, print(f(-)), nl, print([-, +]), nl, print(1 - (-)), nl, print(- (1 + 2)), nl.\n",
               "- (-)\nf(-)\n[-,+]\n1-(-)\n- (1+2)\n", "operators as atoms");
    expect_out(G, ":- initialization(main).\nmain :- print(2 ^ 3 ^ 4), nl, print((2 ^ 3) ^ 4), nl, print(- 2 ^ 2), nl, print(-(2) ^ 2), nl, print(-2 ^ 2), nl.\n",
               "2^3^4\n(2^3)^4\n- 2^2\n(- 2)^2\n-2^2\n", "xfy and prefix minus");
    expect_out(G, ":- initialization(main).\nmain :- print(a:b:c), nl, print((a:b):c), nl, print('|'(a, b)), nl, print(','(a, b)), nl.\n",
               "a:b:c\n(a:b):c\na|b\na,b\n", "more infix");
    expect_out(G, ":- initialization(main).\nmain :- print(0'a), nl, print(0' ), nl.\n",
               "97\n32\n", "character codes");
    expect_out(G, ":- initialization(main).\nmain :- print(dynamic(x)), nl, print(- dynamic), nl, print(f(:-)), nl, print(:-), nl.\n",
               "dynamic x\n- (dynamic)\nf(:-)\n:-\n", "prefix operators as atoms");
    expect_out(G, ":- initialization(main).\nmain :- nl, nl, write(x), nl.\n",
               "\n\nx\n", "nl");
    expect_out(G, ":- initialization(main).\nmain :- tab(3), write(x), tab(1 + 1), write(y), tab(-1), nl.\n",
               "   x  y\n", "tab evaluates");
    expect_out(G, ":- initialization(main).\nmain :- write(a), write(b), write(1), write(2), nl.\n",
               "ab12\n", "writes do not space");
    expect_out(G, ":- initialization(main).\nmain :- format(\"hello~n\"), format('atom ~w~n', [x]), format(\"~w ~a ~d~n\", [f(x), abc, 42]).\n",
               "hello\natom x\nf(x) abc 42\n", "format");
    expect_out(G, ":- initialization(main).\nmain :- format(\"~q ~p ~w~n\", ['A b', 'C', 'D']), format(\"~~ ~c~i~w~n\", [65, skip, end]).\n",
               "'A b' 'C' D\n~ Aend\n", "format quoting and codes");
    expect_out(G, ":- initialization(main).\nmain :- format(\"~w~n\", hello), format(\"~a~n\", [12]), format(\"no~n\", []).\n",
               "hello\n12\nno\n", "format argument forms");
    expect_out(G, ":- initialization(main).\nmain :- format(\"a~nb~n~n\").\n",
               "a\nb\n\n", "format newlines");
    expect_out(G, ":- initialization(main).\np(1). p(2). p(3).\nmain :- p(X), write(X), nl, fail.\nmain.\n",
               "1\n2\n3\n", "backtracking through facts");
    expect_out(G, ":- initialization(main).\np(1). p(2). p(3).\nmain :- p(X), X > 1, !, write(X), nl.\n",
               "2\n", "cut commits");
    expect_out(G, ":- initialization(main).\nq(X) :- X > 5, !, write(big). q(_) :- write(small).\nmain :- q(9), nl, q(1), nl.\n",
               "big\nsmall\n", "cut selects a clause");
    expect_out(G, ":- initialization(main).\nmain :- ( member(X, [1, 2, 3]), X > 1 -> write(X) ; write(none) ), nl.\n",
               "2\n", "if-then-else");
    expect_out(G, ":- initialization(main).\nmain :- ( fail -> write(a) ; write(b) ), nl, ( true -> write(c) ), nl.\n",
               "b\nc\n", "if-then-else branches");
    expect_out(G, ":- initialization(main).\nmain :- ( member(X, [1, 2]) ; X = 3 ), write(X), nl, fail.\nmain.\n",
               "1\n2\n3\n", "disjunction");
    expect_out(G, ":- initialization(main).\nmain :- \\+ member(z, [a]), write(yes), nl, ( \\+ member(a, [a]) -> write(no) ; write(ok) ), nl.\n",
               "yes\nok\n", "negation");
    expect_out(G, ":- initialization(main).\nmain :- not(fail), write(ok), nl.\n",
               "ok\n", "not/1");
    expect_out(G, ":- initialization(main).\nmain :- once(member(X, [a, b])), write(X), nl, ignore(fail), write(on), nl.\n",
               "a\non\n", "once and ignore");
    expect_out(G, ":- initialization(main).\nmain :- forall(member(X, [1, 2, 3]), X > 0), write(all), nl, ( forall(member(X, [1, -2]), X > 0) -> true ; write(notall) ), nl.\n",
               "all\nnotall\n", "forall");
    expect_out(G, ":- initialization(main).\nmain :- findall(X-Y, (member(X, [1, 2]), member(Y, [a, b])), L), print(L), nl, findall(Z, fail, E), print(E), nl.\n",
               "[1-a,1-b,2-a,2-b]\n[]\n", "findall");
    expect_out(G, ":- initialization(main).\nmain :- findall(X, (member(X, [1, 2, 3]), !), L), print(L), nl.\n",
               "[1]\n", "cut inside findall is local");
    expect_out(G, ":- initialization(main).\nmain :- ( member(X, [1, 2, 3]), X > 1, ! ; X = 0 ), write(X), nl.\n",
               "2\n", "cut in a disjunction cuts the clause");
    expect_out(G, ":- initialization(main).\nt :- ( member(X, [1, 2, 3]), !, X > 1 -> write(yes) ; write(no) ), nl.\nmain :- t.\n",
               "no\n", "cut in a condition is local");
    expect_out(G, ":- initialization(main).\nmain :- \\+ (member(X, [1, 2]), !, X > 1), write(ok), nl.\n",
               "ok\n", "cut inside negation is local");
    expect_out(G, ":- initialization(main).\nmain :- X = f(Y), Y = 1, print(X), nl, ( a = b -> true ; write(ne) ), nl, f(A, b) = f(a, B), print(A/B), nl.\n",
               "f(1)\nne\na/b\n", "unification");
    expect_out(G, ":- initialization(main).\nmain :- ( a \\= b -> write(yes) ; write(no) ), ( X \\= 1 -> write(yes) ; write(no) ), nl, var(X).\n",
               "yesno\n", "not unifiable");
    expect_out(G, ":- initialization(main).\nmain :- ( f(X) == f(X) -> write(a) ; write(b) ), ( f(X) == f(_) -> write(a) ; write(b) ), ( a \\== b -> write(c) ; write(d) ), nl.\n",
               "abc\n", "identity");
    expect_out(G, ":- initialization(main).\nmain :- X = Y, Y = 3, write(X), nl, [H|T] = [1, 2, 3], print(H-T), nl.\n",
               "3\n1-[2,3]\n", "aliasing and lists");
    expect_out(G, ":- initialization(main).\nmain :- true, write(t), nl, ( false -> write(f) ; write(nf) ), nl.\n",
               "t\nnf\n", "true and false");
    expect_out(G, ":- initialization(main).\nlen([], 0).\nlen([_|T], N) :- len(T, M), N is M + 1.\nmain :- len([a, b, c], N), write(N), nl.\n",
               "3\n", "recursion");
    expect_out(G, ":- initialization(main).\ncount(N, N) :- !.\ncount(I, N) :- J is I + 1, count(J, N).\nmain :- count(0, 20000), write(done), nl.\n",
               "done\n", "deep tail recursion runs in constant space");
    expect_out(G, ":- initialization(main).\nnat(0).\nnat(N) :- nat(M), N is M + 1.\nmain :- nat(X), X >= 3, !, write(X), nl.\n",
               "3\n", "generate and test");
    expect_out(G, ":- initialization(main).\nk(1, one). k(2, two). k(f(x), fx). k([a], list). k([], nil). k(a, atom).\nmain :- k(f(x), A), k([a], B), k([], C), k(a, D), k(2, E), print([A, B, C, D, E]), nl, findall(K-V, k(K, V), L), length(L, N), write(N), nl.\n",
               "[fx,list,nil,atom,two]\n6\n", "first-argument indexing");
    expect_out(G, ":- initialization(a).\n:- initialization(b).\na :- write(a), nl, fail.\nb :- write(b), nl.\n",
               "a\nb\n", "a failing initialization goal is followed by the next");
    expect_out(G, ":- initialization(a).\n:- initialization(b).\na :- write(a), nl, halt.\nb :- write(b), nl.\n",
               "a\n", "halt stops");
    expect_out(G, ":- initialization(main).\n% a comment\nmain :- write(x), /* inline */ nl. % trailing\n",
               "x\n", "comments");
    expect_out(G, ":- initialization(main).\nt(X) :- Y is X, write(Y), nl.\nmain :- t(7 // 2), t(-7 // 2), t(7 mod -2), t(-7 mod 2), t(7 rem -2), t(-7 rem 2), t(-7 div 2), t(7 div -2).\n",
               "3\n-3\n-1\n1\n1\n-1\n-4\n-4\n", "integer division");
    expect_out(G, ":- initialization(main).\nt(X) :- Y is X, write(Y), nl.\nmain :- t(6 / 3), t(2 ^ 10), t(2 ** 10), t((-2) ^ 3), t(1 ^ -5), t(-1 ^ -3), t(0 ^ 0).\n",
               "2\n1024\n1024\n-8\n1\n-1\n1\n", "exact division and powers");
    expect_out(G, ":- initialization(main).\nt(X) :- Y is X, write(Y), nl.\nmain :- t(min(3, -4)), t(max(3, -4)), t(abs(-9)), t(sign(-9)), t(sign(0)), t(- (3)), t(+ 3), t([5]).\n",
               "-4\n3\n9\n-1\n0\n-3\n3\n5\n", "functions");
    expect_out(G, ":- initialization(main).\nt(X) :- Y is X, write(Y), nl.\nmain :- t(5 /\\ 3), t(5 \\/ 3), t(5 xor 3), t(\\ 5), t(1 << 10), t(-16 >> 2).\n",
               "1\n7\n6\n-6\n1024\n-4\n", "bit operations");
    expect_out(G, ":- initialization(main).\nmain :- X = 2 + 3, Y is X * 2, write(Y), nl, Z = f(1), ( Z = f(A), B is A + 1 -> write(B) ; true ), nl.\n",
               "10\n2\n", "evaluating bound terms");
    expect_out(G, ":- initialization(main).\nmain :- ( 1 =:= 1 -> write(a) ; write(b) ), ( 1 =\\= 1 -> write(a) ; write(b) ), ( 1 < 2 -> write(a) ; write(b) ), ( 2 > 1 -> write(a) ; write(b) ), ( 2 =< 2 -> write(a) ; write(b) ), ( 1 >= 2 -> write(a) ; write(b) ), nl.\n",
               "abaaab\n", "comparisons");
    expect_out(G, ":- initialization(main).\nmain :- ( 1 + 1 =:= 2 -> write(yes) ; write(no) ), ( 3 is 1 + 2 -> write(yes) ; write(no) ), ( 2 is 1 + 2 -> write(yes) ; write(no) ), nl.\n",
               "yesyesno\n", "comparing values");
    expect_out(G, ":- initialization(main).\nmain :- ( var(_) -> write(y) ; write(n) ), ( var(a) -> write(y) ; write(n) ), ( nonvar(a) -> write(y) ; write(n) ), ( atom(a) -> write(y) ; write(n) ), ( atom([]) -> write(y) ; write(n) ), ( atom(1) -> write(y) ; write(n) ), ( number(1) -> write(y) ; write(n) ), ( integer(a) -> write(y) ; write(n) ), nl.\n",
               "ynyynnyn\n", "type tests");
    expect_out(G, ":- initialization(main).\nmain :- ( atomic(a) -> write(y) ; write(n) ), ( atomic(f(x)) -> write(y) ; write(n) ), ( compound(f(x)) -> write(y) ; write(n) ), ( compound([a]) -> write(y) ; write(n) ), ( compound(a) -> write(y) ; write(n) ), ( callable(a) -> write(y) ; write(n) ), ( callable(f(x)) -> write(y) ; write(n) ), ( callable(1) -> write(y) ; write(n) ), ( is_list([a]) -> write(y) ; write(n) ), ( is_list([a|_]) -> write(y) ; write(n) ), nl.\n",
               "ynyynyynyn\n", "more type tests");
    expect_out(G, ":- initialization(main).\nmain :- append(X, Y, [1, 2]), print(X/Y), nl, fail.\nmain :- append([a], [b], L), print(L), nl.\n",
               "[]/[1,2]\n[1]/[2]\n[1,2]/[]\n[a,b]\n", "append");
    expect_out(G, ":- initialization(main).\nmain :- member(X, [a, b]), write(X), nl, fail.\nmain :- ( memberchk(b, [a, b, c]) -> write(yes) ; true ), nl.\n",
               "a\nb\nyes\n", "member and memberchk");
    expect_out(G, ":- initialization(main).\nmain :- reverse([1, 2, 3], R), print(R), nl, ( reverse(a, _) -> true ; write(no) ), nl.\n",
               "[3,2,1]\nno\n", "reverse");
    expect_out(G, ":- initialization(main).\nmain :- nth0(1, [a, b, c], X), nth1(1, [a, b, c], Y), print(X/Y), nl, findall(I-E, nth0(I, [x, y], E), L), print(L), nl, ( nth0(5, [a], _) -> true ; write(no) ), nl.\n",
               "b/a\n[0-x,1-y]\nno\n", "nth0 and nth1");
    expect_out(G, ":- initialization(main).\nmain :- last([1, 2, 3], X), sum_list([1, 2, 3], S), sumlist([], Z), max_list([3, 9, 2], M), min_list([3, 9, 2], N), print([X, S, Z, M, N]), nl, ( max_list([], _) -> true ; write(empty) ), nl.\n",
               "[3,6,0,9,2]\nempty\n", "last, sums and extremes");
    expect_out(G, ":- initialization(main).\nmain :- numlist(3, 7, L), print(L), nl, ( numlist(3, 2, _) -> true ; write(no) ), nl.\n",
               "[3,4,5,6,7]\nno\n", "numlist");
    expect_out(G, ":- initialization(main).\nmain :- length([a, b], N), write(N), nl, length(L, 2), L = [x, y], print(L), nl, length([a|T], 3), T = [b, c], print(T), nl, length(Q, K), K >= 2, !, Q = [p, q], print(Q), nl.\n",
               "2\n[x,y]\n[b,c]\n[p,q]\n", "length");
    expect_out(G, ":- initialization(main).\nmain :- findall(X, between(1, 5, X), L), print(L), nl, ( between(1, 3, 3) -> write(y) ; write(n) ), ( between(3, 1, _) -> write(y) ; write(n) ), nl, between(5, inf, Y), Y > 7, !, write(Y), nl.\n",
               "[1,2,3,4,5]\nyn\n8\n", "between");
    expect_out(G, ":- initialization(main).\nappend(mine, mine, mine).\nmain :- append(A, B, C), print(A-B-C), nl.\n",
               "mine-mine-mine\n", "a program may define a library predicate");
    expect_out(G, ":- initialization(main).\nmain :- X = f(X), write(x), nl.\n",
               "x\n", "no occurs check: X = f(X) binds");
    expect_out(G, ":- initialization(main).\nmain :- X = '.', writeq(X), nl, writeq(f('.', a)), nl.\n",
               "'.'\nf('.',a)\n", "the atom '.' itself");
    expect_out(G, ":- initialization(main).\nmain :- X = 4611686018427387903, Y is -X, write(X/Y), nl.\n",
               "4611686018427387903/ -4611686018427387903\n", "the largest integer literal");
    expect_trap(G, ":- initialization(main).\nmain :- X is 4611686018427387903 + 1, write(X).\n", ZL_E_OVERFLOW, 2, "past 62 bits (SWI has bignums)");
    expect_trap(G, ":- initialization(main).\nmain :- X is 2 ^ 62, write(X).\n", ZL_E_OVERFLOW, 2, "a power past 62 bits");
    expect_trap(G, ":- initialization(main).\nmain :- X is 1 // 0, write(X).\n", ZL_E_DIVZERO, 2, "integer division by zero");
    expect_trap(G, ":- initialization(main).\nmain :- X is 1 mod 0, write(X).\n", ZL_E_DIVZERO, 2, "mod by zero");
    expect_trap(G, ":- initialization(main).\nmain :- X is 7 / 2, write(X).\n", ZL_E_INEXACT, 2, "/ that would make a float");
    expect_trap(G, ":- initialization(main).\nmain :- X is 2 ^ -1, write(X).\n", ZL_E_INEXACT, 2, "a negative power that would make a float");
    expect_trap(G, ":- initialization(main).\nmain :- X is Y + 1, write(X-Y).\n", ZL_E_DOMAIN, 2, "arithmetic on an unbound variable");
    expect_trap(G, ":- initialization(main).\nmain :- Y = a, X is Y + 1, write(X).\n", ZL_E_DOMAIN, 2, "arithmetic on an atom");
    expect_trap(G, ":- initialization(main).\nmain :- Y = f(1), X is Y, write(X).\n", ZL_E_DOMAIN, 2, "a functor that is not evaluable");
    expect_trap(G, ":- initialization(main).\nmain :- write(f(_)), nl.\n", ZL_E_DOMAIN, 2, "writing an unbound variable");
    expect_trap(G, ":- initialization(main).\nmain :- X = f(X), write(X).\n", ZL_E_RSTACK, 2, "writing a cyclic term");
    expect_trap(G, ":- initialization(main).\nmain :- format(\"~d~n\", [a]).\n", ZL_E_DOMAIN, 2, "~d of an atom");
    expect_trap(G, ":- initialization(main).\nmain :- format(\"~a~n\", [f(x)]).\n", ZL_E_DOMAIN, 2, "~a of a compound");
    expect_trap(G, ":- initialization(main).\nmain :- format(\"~a~n\", [[]]).\n", ZL_E_DOMAIN, 2, "~a of []");
    expect_trap(G, ":- initialization(main).\nmain :- format(\"~w ~w~n\", [a]).\n", ZL_E_DOMAIN, 2, "too few format arguments");
    expect_trap(G, ":- initialization(main).\nmain :- format(\"~w~n\", [a, b]).\n", ZL_E_DOMAIN, 2, "too many format arguments");
    expect_trap(G, ":- initialization(main).\nmain :- length(_, -1).\n", ZL_E_DOMAIN, 0, "length with a negative length");
    expect_trap(G, ":- initialization(main).\nmain :- length(a, _).\n", ZL_E_DOMAIN, 0, "length of a non-list");
    expect_trap(G, ":- initialization(main).\nmain :- between(a, 3, _).\n", ZL_E_DOMAIN, 0, "between with an atom bound");
    expect_trap(G, ":- initialization(main).\nmain :- nth0(a, [x], _).\n", ZL_E_DOMAIN, 0, "nth0 with an atom index");
    expect_trap(G, ":- initialization(main).\nmain :- sum_list([a], _).\n", ZL_E_DOMAIN, 0, "sum_list of an atom");
    expect_trap(G, ":- initialization(main).\nloop :- loop.\nmain :- loop.\n", ZL_E_FUEL, 0, "an endless loop runs out of fuel");
    expect_refused(G, ":- initialization(main).\nmain :- X is 1.5, write(X).\n", ZL_E_UNSUPPORTED, 2, "floats");
    expect_refused(G, ":- initialization(main).\nmain :- X = \"abc\", write(X).\n", ZL_E_UNSUPPORTED, 2, "strings as values");
    expect_refused(G, ":- initialization(main).\nmain :- X = `abc`, write(X).\n", ZL_E_UNSUPPORTED, 2, "back quotes");
    expect_refused(G, ":- initialization(main).\nmain :- assert(foo(1)).\n", ZL_E_UNSUPPORTED, 2, "assert");
    expect_refused(G, ":- initialization(main).\nmain :- retract(foo(1)).\n", ZL_E_UNSUPPORTED, 2, "retract");
    expect_refused(G, ":- initialization(main).\nmain :- catch(true, _, true).\n", ZL_E_UNSUPPORTED, 2, "catch");
    expect_refused(G, ":- initialization(main).\nmain :- throw(x).\n", ZL_E_UNSUPPORTED, 2, "throw");
    expect_refused(G, ":- initialization(main).\nmain :- G = true, G.\n", ZL_E_UNSUPPORTED, 2, "a variable as a goal");
    expect_refused(G, ":- initialization(main).\nmain :- call(true).\n", ZL_E_UNSUPPORTED, 2, "call/1");
    expect_refused(G, ":- initialization(main).\nmain :- atom_length(abc, N), write(N).\n", ZL_E_UNSUPPORTED, 2, "a built-in outside the subset");
    expect_refused(G, ":- initialization(main).\n:- dynamic(foo/1).\nmain.\n", ZL_E_UNSUPPORTED, 2, "other directives");
    expect_refused(G, ":- initialization(main).\n:- op(700, xfx, ===).\nmain.\n", ZL_E_UNSUPPORTED, 2, "op/3");
    expect_refused(G, ":- initialization(main).\ngreeting --> [hello].\nmain.\n", ZL_E_UNSUPPORTED, 2, "DCG rules");
    expect_refused(G, ":- initialization(main).\nmain :- ( true *-> write(a) ; write(b) ).\n", ZL_E_UNSUPPORTED, 2, "the soft cut");
    expect_refused(G, ":- initialization(main).\nmain :- undefined_thing.\n", ZL_E_UNDEFINED, 2, "a call to an undefined predicate");
    expect_refused(G, ":- initialization(main).\nwrite(_).\nmain.\n", ZL_E_UNSUPPORTED, 2, "defining a built-in");
    expect_refused(G, ":- initialization(main).\nportray(_).\nmain.\n", ZL_E_UNSUPPORTED, 2, "defining a hook");
    expect_refused(G, ":- initialization(main).\nmain :- X = 0x1F, write(X).\n", ZL_E_UNSUPPORTED, 2, "hexadecimal numbers");
    expect_refused(G, ":- initialization(main).\nmain :- X = 99999999999999999999, write(X).\n", ZL_E_UNSUPPORTED, 2, "an integer past 62 bits");
    expect_refused(G, ":- initialization(main).\nmain :- X = '\\x41\\', write(X).\n", ZL_E_UNSUPPORTED, 2, "escapes other than \\n \\t");
    expect_refused(G, ":- initialization(main).\nmain :- X = '$x', write(X).\n", ZL_E_UNSUPPORTED, 2, "names beginning with $");
    expect_refused(G, ":- initialization(main).\nmain :- X = '[]', write(X).\n", ZL_E_UNSUPPORTED, 2, "the quoted atom '[]'");
    expect_refused(G, ":- initialization(main).\nmain :- format(\"~e~n\", [1]).\n", ZL_E_UNSUPPORTED, 2, "float format directives");
    expect_refused(G, ":- initialization(main).\nmain :- format(\"~t~w~10|~n\", [a]).\n", ZL_E_UNSUPPORTED, 2, "column stops");
    expect_refused(G, ":- initialization(main).\nmain :- format(\"~2d~n\", [100]).\n", ZL_E_UNSUPPORTED, 2, "numeric directive arguments");
    expect_refused(G, ":- initialization(main).\nmain :- L = [a], format(\"~w~n\", L).\n", ZL_E_UNSUPPORTED, 2, "format arguments in a variable");
    expect_refused(G, ":- initialization(main).\nmain :- F = \"~w\", format(F, [a]).\n", ZL_E_UNSUPPORTED, 2, "a format control in a variable");
    expect_refused(G, ":- initialization(main).\nmain :- X is pi, write(X).\n", ZL_E_UNSUPPORTED, 2, "pi");
    expect_refused(G, ":- initialization(main).\nmain :- X is sqrt(4), write(X).\n", ZL_E_UNSUPPORTED, 2, "sqrt");
    expect_refused(G, ":- initialization(main).\nmain :- write(a).\nmain :- write((a | b)).\n", ZL_E_SYNTAX, 3, "the bar as an infix operator");
    expect_refused(G, ":- initialization(main).\nmain :- write(a)\n", ZL_E_SYNTAX, 0, "a missing full stop");
    expect_refused(G, ":- initialization(main).\nmain :- write((a).\n", ZL_E_SYNTAX, 2, "unbalanced parentheses");
    expect_refused(G, ":- initialization(main).\nmain :- X = f(a :- b :- c).\n", ZL_E_SYNTAX, 2, "a priority clash");
    expect_refused(G, ":- initialization(main).\nmain :- write(- = a).\n", ZL_E_UNSUPPORTED, 2, "a prefix operator before an infix one");
    expect_refused(G, ":- initialization(main).\nmain :- X = '.'(a, b), write(X).\n", ZL_E_UNSUPPORTED, 2, "a '.'(...) term (SWI 7 dict call)");
    expect_refused(G, ":- initialization(main).\nmain :- X = a, Y = X.b, write(Y).\n", ZL_E_UNSUPPORTED, 2, "the infix '.' (SWI 7 dict call)");
    expect_refused(G, ":- initialization(main).\nmain :- X = 4611686018427387904, write(X).\n", ZL_E_UNSUPPORTED, 2, "an integer of exactly 2^62");
    expect_refused(G, ":- initialization(main).\nmain :- X = 9223372036854775808, write(X).\n", ZL_E_UNSUPPORTED, 2, "an integer past 64 bits");
    expect_twin(G, "main:-write(hello),nl.\n",
                "main :-\n    write(hello),\n    nl.\n");
    expect_twin(G, ":- initialization(main).\n% about main\nmain :- X = 'abc', print([X|T]), T = [].\nfact(0'a) :- format(\"s~n\").\n",
                ":- initialization(main).\n\n% about main\nmain :-\n    X = abc,\n    print([X|T]),\n    T = [].\n\nfact(97) :-\n    format(\"s~n\").\n");
    expect_twin(G, "p(X) :- ( X > 1 -> Y is X*2 ; Y = - 1 ), write(Y-(-(1))), \\+X=a.\n",
                "p(X) :-\n    (X > 1 -> Y is X * 2 ; Y = - 1),\n    write(Y - - 1),\n    \\+ X = a.\n");
    expect_twin(G, "q(f(- , a), (-)-(-), 'A b', [a,b|c], {x,y}, f((a,b)), (a:-b)).\n",
                "q(f(-, a), (-) - (-), 'A b', [a, b|c], {x, y}, f((a, b)), (a :- b)).\n");
    expect_twin(G, "a.\na.\nb :- a.\n\n\n:- initialization(b).\n",
                "a.\na.\n\nb :-\n    a.\n\n:- initialization(b).\n");
    expect_twin(G, "a. b. c. d.\nr :- ((a, b), c), d.\n",
                "a.\n\nb.\n\nc.\n\nd.\n\nr :-\n    a,\n    b,\n    c,\n    d.\n");
    /* a compiled program is a 62-bit Prolog program */
    {
        zl_diag_t d;
        CHECK(compile(G, ":- initialization(main).\nmain :- write(1), nl.\n", &d) == ZL_OK &&
                  P.word_bits == 62 && P.lang == G,
              "Prolog programs are 62-bit");
        CHECK(compile(G, "", &d) == ZL_OK && run_p(&P, 0, 0, 0) == ZL_OK && out[0] == 0,
              "the empty program");
        CHECK(compile(G, "% only a comment\n", &d) == ZL_OK && run_p(&P, 0, 0, 0) == ZL_OK,
              "only a comment");
        CHECK(compile(G, "p(1).\n", &d) == ZL_OK && run_p(&P, 0, 0, 0) == ZL_OK && out[0] == 0,
              "no initialization goal: nothing runs");
        CHECK(compile(G, ":- initialization(main).\nmain :- write(x)", &d) == ZL_E_SYNTAX,
              "the last clause needs its full stop");
        CHECK(compile(G, ":- initialization(main).\nmain :- write(x).", &d) == ZL_OK,
              "no newline at the end");
        CHECK(run_src(G, ":- initialization(main).\nmain :- between(1, 100000, _), fail.\nmain.\n",
                      0, 0, 1000) == ZL_E_FUEL,
              "fuel stops a long search");
    }
    /* backtracking undoes bindings made before the choicepoint only */
    expect_out(G,
               ":- initialization(main).\n"
               "p(X, Y) :- ( X = 1, Y = a ; X = 2, Y = b ).\n"
               "main :- p(X, Y), write(X-Y), nl, Y == b, !, write(done), nl.\n",
               "1-a\n2-b\ndone\n", "bindings are undone on backtracking");
    /* recursion deeper than the IR call stack: resolution keeps its frames
     * in data, so only the heap bounds it */
    expect_out(G,
               ":- initialization(main).\n"
               "mk(0, []) :- !.\nmk(N, [N|T]) :- M is N - 1, mk(M, T).\n"
               "len([], 0).\nlen([_|T], N) :- len(T, M), N is M + 1.\n"
               "main :- mk(3000, L), len(L, N), write(N), nl.\n",
               "3000\n", "recursion 3000 deep");
    /* limits */
    {
        static char big[1 << 15];
        size_t n = (size_t) snprintf(big, sizeof big, ":- initialization(main).\nmain :- X = ");
        for (int i = 0; i < 300; i++) {
            int w_ = snprintf(big + n, sizeof big - n, "f(");
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        {
            int w_ = snprintf(big + n, sizeof big - n, "a");
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        for (int i = 0; i < 300; i++) {
            int w_ = snprintf(big + n, sizeof big - n, ")");
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        snprintf(big + n, sizeof big - n, ", write(X).\n");
        expect_refused(G, big, ZL_E_LIMIT, 0, "terms nested too deeply");
        n = (size_t) snprintf(big, sizeof big, ":- initialization(main).\nmain :- X = ");
        for (int i = 0; i < 40; i++) {
            int w_ = snprintf(big + n, sizeof big - n, "f(");
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        {
            int w_ = snprintf(big + n, sizeof big - n, "a");
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        for (int i = 0; i < 40; i++) {
            int w_ = snprintf(big + n, sizeof big - n, ")");
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        snprintf(big + n, sizeof big - n, ", X = Y, write(Y), nl.\n");
        CHECK(run_src(G, big, 0, 0, 0) == ZL_OK && strlen(out) == 40 * 3 + 2,
              "a term 40 deep is built and written");
        n = (size_t) snprintf(big, sizeof big, ":- initialization(main).\nmain :- X = [");
        for (int i = 0; i < 300; i++) {
            int w_ = snprintf(big + n, sizeof big - n, "%s%d", i ? "," : "", i);
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        snprintf(big + n, sizeof big - n, "], write(X).\n");
        expect_refused(G, big, ZL_E_LIMIT, 2, "a list literal longer than 256 elements");
        n = 0;
        for (int i = 0; i < 500 && n < sizeof big - 32; i++) {
            int w_ = snprintf(big + n, sizeof big - n, "p%d.\n", i);
            if (w_ < 0 || (size_t) w_ >= (size_t) (sizeof big - n)) abort();
            n += (size_t) w_;
        }
        expect_refused(G, big, ZL_E_LIMIT, 0, "too many predicates");
    }
}
