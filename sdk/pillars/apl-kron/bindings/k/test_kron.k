/ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
/ SPDX-License-Identifier: Apache-2.0
/ test_kron.k - the ngn/k binding. Run: k test_kron.k /path/libzkron_k.so
/ (make bindings-k NGNK=... NGNK_SRC=...). Exits 1 on the first failure.

kronlib:x 0
\l kron.k

n:0
t:{n::n+1;$[x;::;[`0:"FAIL ",y;`exit 1]]}
e:{r:.[{x[y];0};(x;y);{x}];$[0~r;0;1_r@!r?"\n"]}   / the error a call raises, or 0

/ values, exactly
t[(kron "+/1r3 1r6")~"1r2";"rational sum"]
t[(kron "1%3")~"1r3";"K % is exact division"]
t[(kron "-/1 2 3 4")~"-8";"K / folds from the left"]
t[(kron "-/1 2 3 4")~kron "kron\n(foldl sub (vec 1 2 3 4))";"canonical text"]
t[(kron "+/1 2 3 4")~kron "+/1 2 3 4";"deterministic"]
t[(kron "1 2 3<2")~"(tvec true false false)";"comparisons are truth values"]
t[(kronshow "2 3#!6")~"0 1 2\n3 4 5";"display"]
t[(kroncanon "x:1;x+1")~"kron\n(let x 1)\n(add x 1)\n";"canonical program"]
t[64=#krondigest "1+1";"digest length"]
t[(krondigest "1+1")~krondigest "1 + 1";"digest ignores spacing"]
t[(krondigest "+/1 2 3")~krondigest "+/1 2 3";"same digest"]
t[~(krondigest "-/1 2 3")~krondigest "kron\n(reduce sub (vec 1 2 3))";"left and right folds differ"]
t[(krondigest "+/1 2 3")~krondigest "kron\n(reduce add (vec 1 2 3))";"associative folds agree"]

/ shape;num;den and native K integers
t[(kronnd "2 2#!4")~(2 2;0 1 2 3;1 1 1 1);"nd matrix"]
t[(kronnd "1r3")~(!0;1;3);"nd scalar"]
t[(kronnd "1#5")~(,1;,5;,1);"nd one-element vector"]
t[(kronnd "1 2<2 1")~(,2;1 2;0 0);"nd truth codes"]
t[(kronint "2 2#!4")~(0 1;2 3);"kronint matrix"]
t[(kronint "+/!10")~45;"kronint scalar"]
t[(kronint "9223372036854775806+1")~9223372036854775807;"int64 edge"]
t[(e[kronint;"1r2"])~"kronint: not whole";"kronint refuses fractions"]
r:kron "+/1r3 1r3 1r3"
t[r~,"1";"1r3+1r3+1r3 is exactly 1"]

/ refusals are K errors named by status
t[(e[kron;"1%0"])~"KR_E_DIVZERO";"division by zero"]
t[0<#kronwhy[];"diagnostic kept"]
t[(e[kron;"9223372036854775807+1"])~"ZXV_E_RANGE";"overflow"]
t[(e[kron;"1 2+1 2 3"])~"KR_E_LENGTH";"length"]
t[(e[kron;"1e3"])~"KR_E_SYNTAX";"no exponents"]
t[(e[kron;"*1 2"])~"KR_E_SYNTAX";"monadic * is not in KRON"]
t[(e[kron;"^3 1 2"])~"KR_E_SYNTAX";"monadic ^ is null in ngn/k, sort in k9: refused"]
t[(e[kron;"1<=2"])~"KR_E_SYNTAX";"<= is < and = in ngn/k: refused, never reread"]
t[(kron "1 2 3~1 2 3")~"true";"match is a truth value"]
t[(e[kron;"y+1"])~"KR_E_NAME";"free name"]
t[(e[kx[9];"1"])~"ZXV_E_ARG";"bad selector"]
t[(e[kron;`a])~"ZXV_E_ARG";"not a char vector"]
t[(kron "1+1")~,"2";"works after refusals"]
t[0=#kronwhy[];"diagnostic cleared"]

/ the same text through ngn/k itself (integer results): KRON keeps K's
/ meaning where it has one
cross:("+/!10";"-/1 2 3 4";"*/1+!10";"|/3 1 4 1 5";"&/3 1 4 1 5";"+\\1 2 3 4";"-\\1 2 3 4")
cross,:("2 3#!6";"+2 3#!6";"|!5";"|3 2#!6";"+/3 2#!6";"(!3)*\\:!4";"2#!5";"2_!5";"7#1 2 3")
cross,:("<3 1 4 1 5";">3 1 4 1 5";"3 1 4?4";"(!5)@3 1";"5#1 2";"1 2 3,4 5";"(2 2#!4),1 2#9 8")
cross,:("#!7";"3|&/5 7";"-!4";"y@<y:3 1 2";"_7%2";"(!3)+\\:!2";"-2_!5";"3 2#!4")
ok:{.[{(. x)~kronint x};,x;{0}]}'cross
`0:cross@&~ok
t[&/ok;"KRON and ngn/k agree on ",($#cross)," programs"]

/ references: many calls must not leak or double free
r:{kron "+/!",$x}'!2000
t[(r 100)~"4950";"repeated calls"]
t[(r 1999)~$+/!1999;"repeated calls, last"]
q:{e[kron;"1%0"]}'!500
t[&/q~\:"KR_E_DIVZERO";"repeated refusals"]

`0:"test_kron.k: ",($n)," checks, 0 failed"
`exit 0
