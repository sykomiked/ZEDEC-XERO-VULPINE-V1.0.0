/* test_zl_algol.inc — ALGOL 60 front-end tests, included by test_zl.c. */

#define AL ZL_LANG_ALGOL60

/* the value of one integer expression, printed by outinteger */
static void alg_e(const char *expr, const char *want)
{
    static char src[1024];
    snprintf(src, sizeof src, "begin\n  outinteger(1, %s)\nend\n", expr);
    expect_out(AL, src, want, expr);
}

/* statements run in a block with a few declarations */
static void alg_s(const char *body, const char *want)
{
    static char src[4096];
    snprintf(src, sizeof src,
             "begin integer i, j, k, n; Boolean p, q; integer array a[1:10];\n%s\nend\n", body);
    expect_out(AL, src, want, body);
}

/* a trap with the line it is reported on (the statement is on line 2) */
static void alg_trap(const char *body, int status, const char *what)
{
    static char src[4096];
    snprintf(src, sizeof src,
             "begin integer i, j, k, n; Boolean p, q; integer array a[1:10];\n%s\nend\n", body);
    expect_trap(AL, src, status, 2, what);
    twin_props(AL, src, strlen(src), what);
}

/* refused at compile time, on line 2 */
static void alg_ref(const char *body, int status, const char *what)
{
    static char src[4096];
    snprintf(src, sizeof src, "begin integer i, j; integer array a[1:10];\n%s\nend\n", body);
    expect_refused(AL, src, status, 2, what);
}

static void alg_in(const char *src, const int64_t *in, size_t nin, int status, const char *want,
                   const char *what)
{
    int s = run_src(AL, src, in, nin, 0);
    CHECK(s == status && strcmp(out, want) == 0, "%s: status %d out [%s] want %d [%s]", what, s,
          out, status, want);
    twin_props(AL, src, strlen(src), what);
}

static void test_algol_expr(void)
{
    /* precedence and association, Report 3.3.5 */
    alg_e("1 + 2 * 3", "7 ");
    alg_e("(1 + 2) * 3", "9 ");
    alg_e("1 - 2 - 3", "-4 ");
    alg_e("1 - (2 - 3)", "2 ");
    alg_e("2 * 3 + 4 * 5", "26 ");
    alg_e("100 % 7 % 2", "7 ");
    alg_e("100 % (7 % 2)", "33 ");
    alg_e("2 ^ 3 ^ 2", "64 ");
    alg_e("2 ^ (3 ^ 2)", "512 ");
    alg_e("2 ** 10", "1024 ");
    alg_e("-2 ^ 2", "-4 ");
    alg_e("(-2) ^ 2", "4 ");
    alg_e("(-2) ^ 3", "-8 ");
    alg_e("-3 * 4", "-12 ");
    alg_e("+3 - 4", "-1 ");
    alg_e("-(3 - 4)", "1 ");
    alg_e("2 * 3 ^ 2", "18 ");
    alg_e("(2 * 3) ^ 2", "36 ");
    alg_e("10 - 2 ^ 3 * 2", "-6 ");
    alg_e("1 + 2 - 3 + 4", "4 ");
    alg_e("6 * 7 % 4", "10 ");
    alg_e("6 * (7 % 4)", "6 ");
    /* % truncates toward zero, Report 3.3.4.2 */
    alg_e("7 % 2", "3 ");
    alg_e("-7 % 2", "-3 ");
    alg_e("(-7) % 2", "-3 ");
    alg_e("7 % (-2)", "-3 ");
    alg_e("(-7) % (-2)", "3 ");
    alg_e("1 % 2", "0 ");
    alg_e("0 % 5", "0 ");
    alg_e("7 div 2", "3 ");
    alg_e("9 - 9 % 4 * 4", "1 ");
    /* / is exact */
    alg_e("12 / 4", "3 ");
    alg_e("-12 / 4", "-3 ");
    alg_e("12 / 4 / 3", "1 ");
    alg_e("12 / (4 / 2)", "6 ");
    alg_e("6 / 3 * 2", "4 ");
    alg_e("3 * 4 / 6", "2 ");
    alg_e("entier(7 / 7)", "1 ");
    alg_e("2 ^ 0", "1 ");
    alg_e("0 ^ 3", "0 ");
    alg_e("1 ^ (-5)", "1 ");
    alg_e("(-1) ^ (-3)", "-1 ");
    alg_e("(-1) ^ 7", "-1 ");
    alg_e("3 ^ 4", "81 ");
    alg_e("2 ^ 62", "4611686018427387904 ");
    alg_e("(-2) ^ 63", "-9223372036854775808 ");
    alg_e("9223372036854775807", "9223372036854775807 ");
    alg_e("-9223372036854775807 - 1", "-9223372036854775808 ");
    alg_e("0", "0 ");
    alg_e("007", "7 ");
    /* the library */
    alg_e("abs(-4)", "4 ");
    alg_e("abs(4)", "4 ");
    alg_e("iabs(-9)", "9 ");
    alg_e("iabs(0)", "0 ");
    alg_e("sign(-9)", "-1 ");
    alg_e("sign(0)", "0 ");
    alg_e("sign(12)", "1 ");
    alg_e("entier(81 / 9)", "9 ");
    alg_e("entier(-3)", "-3 ");
    alg_e("iabs(-2) * sign(-3) + abs(-1)", "-1 ");
    /* conditional expressions */
    alg_e("if 1 < 2 then 10 else 20", "10 ");
    alg_e("if 1 > 2 then 10 else 20", "20 ");
    alg_e("if 1 > 2 then 10 else if 2 > 1 then 30 else 40", "30 ");
    alg_e("(if true then 2 else 3) * 5", "10 ");
    alg_e("1 + (if false then 2 else 3)", "4 ");
    alg_e("if 1 = 1 & 2 = 2 then 1 else 0", "1 ");
    alg_e("if 1 = 1 & 2 != 2 then 1 else 0", "0 ");
    /* relations */
    alg_e("if 3 < 4 then 1 else 0", "1 ");
    alg_e("if 4 < 4 then 1 else 0", "0 ");
    alg_e("if 4 <= 4 then 1 else 0", "1 ");
    alg_e("if 5 <= 4 then 1 else 0", "0 ");
    alg_e("if 4 = 4 then 1 else 0", "1 ");
    alg_e("if 4 = 5 then 1 else 0", "0 ");
    alg_e("if 4 >= 4 then 1 else 0", "1 ");
    alg_e("if 3 >= 4 then 1 else 0", "0 ");
    alg_e("if 5 > 4 then 1 else 0", "1 ");
    alg_e("if 4 > 4 then 1 else 0", "0 ");
    alg_e("if 4 != 5 then 1 else 0", "1 ");
    alg_e("if 4 != 4 then 1 else 0", "0 ");
    alg_e("if -1 < 0 then 1 else 0", "1 ");
    alg_e("if 1 + 1 = 2 then 1 else 0", "1 ");
    /* Boolean operators and their order: ! & | -> == */
    alg_e("if !false then 1 else 0", "1 ");
    alg_e("if !true then 1 else 0", "0 ");
    alg_e("if true & false then 1 else 0", "0 ");
    alg_e("if true & true then 1 else 0", "1 ");
    alg_e("if false | false then 1 else 0", "0 ");
    alg_e("if false | true then 1 else 0", "1 ");
    alg_e("if true -> false then 1 else 0", "0 ");
    alg_e("if false -> false then 1 else 0", "1 ");
    alg_e("if false -> true then 1 else 0", "1 ");
    alg_e("if true -> true then 1 else 0", "1 ");
    alg_e("if true == false then 1 else 0", "0 ");
    alg_e("if false == false then 1 else 0", "1 ");
    alg_e("if true | false & false then 1 else 0", "1 ");
    alg_e("if (true | false) & false then 1 else 0", "0 ");
    alg_e("if !true | true then 1 else 0", "1 ");
    alg_e("if !(true | true) then 1 else 0", "0 ");
    alg_e("if false -> false -> false then 1 else 0", "0 ");
    alg_e("if false -> (false -> false) then 1 else 0", "1 ");
    alg_e("if false == false == false then 1 else 0", "0 ");
    alg_e("if true | false == false then 1 else 0", "0 ");
    alg_e("if false & false -> false then 1 else 0", "1 ");
    alg_e("if 1 < 2 == 3 < 4 then 1 else 0", "1 ");
    /* the Report's own symbols, in UTF-8 */
    alg_e("7 \xc3\xb7 2", "3 ");
    alg_e("3 \xc3\x97 4", "12 ");
    alg_e("2 \xe2\x86\x91 5", "32 ");
    alg_e("if 1 \xe2\x89\xa0 2 \xe2\x88\xa7 1 \xe2\x89\xa4 2 then 1 else 0", "1 ");
    alg_e("if \xc2\xac(2 \xe2\x89\xa5 3 \xe2\x88\xa8 false) then 1 else 0", "1 ");
    alg_e("if false \xe2\x8a\x83 false \xe2\x89\xa1 true then 1 else 0", "1 ");
}

static void test_algol_stmt(void)
{
    /* assignment */
    alg_s("i := 5; outinteger(1, i)", "5 ");
    alg_s("i := j := k := 7; outinteger(1, i + j + k)", "21 ");
    alg_s("i := 2; a[i] := i := 3; outinteger(1, a[2]); outinteger(1, i)", "3 3 ");
    alg_s("i := 1; a[i] := i + 1; outinteger(1, a[1])", "2 ");
    alg_s("p := 1 < 2; q := !p; outinteger(1, if p & !q then 1 else 0)", "1 ");
    alg_s("p := q := true; outinteger(1, if p == q then 1 else 0)", "1 ");
    alg_s("outinteger(1, i); outinteger(1, a[10]); outinteger(1, if p then 1 else 0)", "0 0 0 ");
    /* compound statements, empty statements and comments */
    alg_s("begin i := 1; begin j := 2; k := 3 end end; outinteger(1, i + j + k)", "6 ");
    alg_s("; ; i := 4;; outinteger(1, i);", "4 ");
    alg_s("comment nothing here; i := 1; comment again; outinteger(1, i)", "1 ");
    alg_s("begin end; begin ; end; outinteger(1, 9)", "9 ");
    /* if statements */
    alg_s("i := 3; if i > 2 then outinteger(1, 1)", "1 ");
    alg_s("i := 1; if i > 2 then outinteger(1, 1)", "");
    alg_s("i := 1; if i > 2 then outinteger(1, 1) else outinteger(1, 2)", "2 ");
    alg_s("i := 5; if i > 2 then begin i := i * 2; outinteger(1, i) end else outinteger(1, 0)",
          "10 ");
    alg_s("i := 2; if i = 1 then j := 10 else if i = 2 then j := 20 else j := 30; outinteger(1, j)",
          "20 ");
    alg_s("i := 3; if i = 1 then j := 10 else if i = 2 then j := 20 else j := 30; outinteger(1, j)",
          "30 ");
    alg_s("i := 1; if i = 1 then begin if i > 0 then j := 5 end else j := 6; outinteger(1, j)",
          "5 ");
    alg_s("if true then ; outinteger(1, 1)", "1 ");
    alg_s("i := 0; if false then i := 1 else ; outinteger(1, i)", "0 ");
    /* strings and output */
    alg_s("outstring(1, \"hello\")", "hello");
    alg_s("outstring(1, \"a\\nb\\t\\\\\\\"\")", "a\nb\t\\\"");
    alg_s("outstring(1, \"\")", "");
    alg_s("outinteger(1, -5); outstring(1, \"|\")", "-5 |");
    /* for statements, Report 4.6 */
    alg_s("for i := 1 step 1 until 5 do outinteger(1, i)", "1 2 3 4 5 ");
    alg_s("for i := 5 step -2 until 0 do outinteger(1, i)", "5 3 1 ");
    alg_s("for i := 1 step 1 until 0 do outinteger(1, i); outinteger(1, i)", "1 ");
    alg_s("for i := 1 step 1 until 3 do ; outinteger(1, i)", "4 ");
    alg_s("for i := 3 step -1 until 1 do ; outinteger(1, i)", "0 ");
    alg_s("for i := 1, 4, 9 do outinteger(1, i)", "1 4 9 ");
    alg_s("for i := 1, 2 step 3 until 9, 100 do outinteger(1, i)", "1 2 5 8 100 ");
    alg_s("i := 1; for i := i * 2 while i < 50 do outinteger(1, i)", "2 4 8 16 32 ");
    alg_s("for i := 1 step i until 20 do outinteger(1, i)", "1 2 4 8 16 ");
    alg_s("n := 3; for i := 1 step 1 until n do n := n - 1; outinteger(1, i); outinteger(1, n)",
          "3 1 ");
    alg_s("j := 1; for i := 1 step j until 10 do begin j := j + 1; outinteger(1, i) end",
          "1 3 6 10 ");
    alg_s("for i := 1 step 1 until 3 do for j := 1 step 1 until i do outinteger(1, j)",
          "1 1 2 1 2 3 ");
    alg_s("for i := 1 step 1 until 3 do begin a[i] := i * i end; outinteger(1, a[3])", "9 ");
    alg_s("for i := 10 step -3 until 0 do outinteger(1, i); outinteger(1, i)", "10 7 4 1 -2 ");
    alg_s("i := 0; for j := 1 step 1 until 10 do i := i + j; outinteger(1, i)", "55 ");
    alg_s("k := 0; for i := 1, 2 do for j := 3, 4 do k := k * 10 + i * j; outinteger(1, k)",
          "3468 ");
    alg_s("for i := 1 step 1 until 2, 7 while false, 9 do outinteger(1, i)", "1 2 9 ");
    alg_s("for i := -2 step 1 until 2 do outinteger(1, iabs(i))", "2 1 0 1 2 ");
    /* goto and labels */
    alg_s("i := 0; L: i := i + 1; if i < 5 then goto L; outinteger(1, i)", "5 ");
    alg_s("goto done; outinteger(1, 1); done: outinteger(1, 2)", "2 ");
    alg_s("i := 0; L: begin i := i + 1; if i < 3 then go to L end; outinteger(1, i)", "3 ");
    alg_s("begin integer x; x := 1; goto out; x := 2 end; out: outinteger(1, 7)", "7 ");
    alg_s("for i := 1 step 1 until 10 do if i = 4 then goto e; e: outinteger(1, i)", "4 ");
    alg_s("A: B: outinteger(1, 1); goto C; C: ;", "1 ");
    /* inner blocks: scope and shadowing */
    alg_s("i := 1; begin integer i; i := 2; outinteger(1, i) end; outinteger(1, i)", "2 1 ");
    alg_s("begin integer j; j := 5; i := j end; outinteger(1, i)", "5 ");
    alg_s("begin Boolean i; i := true; if i then j := 1 end; outinteger(1, j)", "1 ");
    alg_s("for i := 1, 2 do begin integer t; t := t + i; outinteger(1, t) end", "1 3 ");
    /* arrays */
    alg_s("a[1] := 5; a[10] := 6; outinteger(1, a[1] + a[10])", "11 ");
    alg_s("for i := 1 step 1 until 10 do a[i] := i; outinteger(1, a[a[a[3]]])", "3 ");
    alg_s("begin integer array b[-3:3]; b[-3] := 1; b[3] := 2; outinteger(1, b[-3] * 10 + b[3]) "
          "end",
          "12 ");
    alg_s("begin integer array b, c[0:1], d[5:5]; b[1] := 2; c[0] := 3; d[5] := 4; "
          "outinteger(1, b[1] + c[0] + d[5]) end",
          "9 ");
    alg_s("begin Boolean array b[1:2]; b[2] := true; outinteger(1, if b[2] & !b[1] then 1 else 0) "
          "end",
          "1 ");
    alg_s("n := 4; begin integer array b[1:n]; for i := 1 step 1 until n do b[i] := i; "
          "outinteger(1, b[n]) end",
          "4 ");
    alg_s("for n := 1, 3 do begin integer array b[n:2 * n]; b[2 * n] := n; outinteger(1, b[2 * n]) "
          "end",
          "1 3 ");
    alg_s("n := 2; begin integer array b[1:n + 1]; b[3] := 9; begin integer array c[0:b[3]]; "
          "c[9] := 1; outinteger(1, c[9] + b[3]) end end",
          "10 ");
    alg_s("a[2] := 7; begin integer array a[1:2]; outinteger(1, a[2]) end; outinteger(1, a[2])",
          "0 7 ");
    alg_s("n := 0; begin integer array b[1:n]; outinteger(1, 5) end", "5 ");
    alg_s("n := 200; begin integer array b[1:n]; b[n] := 3; b[1] := 4; outinteger(1, b[n] + b[1]) "
          "end",
          "7 ");
    /* input */
    {
        static const int64_t in[] = {5, -7, 40};
        alg_in("begin integer x, y; ininteger(0, x); ininteger(0, y); outinteger(1, x * y) end", in,
               3, ZL_OK, "-35 ", "ininteger");
        alg_in("begin integer array b[1:3]; integer i; for i := 1 step 1 until 3 do "
               "ininteger(0, b[i]); outinteger(1, b[3] - b[1]) end",
               in, 3, ZL_OK, "35 ", "ininteger into an array");
        alg_in("begin integer x; ininteger(0, x); ininteger(0, x) end", in, 1, ZL_E_INPUT, "",
               "input exhausted");
        alg_in("begin integer x; procedure r(v); integer v; ininteger(0, v); r(x); "
               "outinteger(1, x) end",
               in, 1, ZL_OK, "5 ", "ininteger into a name parameter");
    }
}

static void test_algol_proc(void)
{
    alg_s("begin procedure p; outinteger(1, 1); p; p end", "1 1 ");
    alg_s("begin integer procedure f; f := 42; outinteger(1, f + f) end", "84 ");
    alg_s("begin integer procedure f; ; outinteger(1, f) end", "0 ");
    alg_s("begin integer procedure sq(x); value x; integer x; sq := x * x; "
          "outinteger(1, sq(sq(3))) end",
          "81 ");
    alg_s("begin Boolean procedure odd(x); value x; integer x; odd := x % 2 * 2 != x; "
          "outinteger(1, if odd(3) & !odd(4) then 1 else 0) end",
          "1 ");
    alg_s("begin integer procedure fact(n); value n; integer n; "
          "fact := if n = 0 then 1 else n * fact(n - 1); outinteger(1, fact(20)) end",
          "2432902008176640000 ");
    alg_s("begin integer procedure fib(n); value n; integer n; "
          "fib := if n < 2 then n else fib(n - 1) + fib(n - 2); outinteger(1, fib(15)) end",
          "610 ");
    /* value parameters are copies, name parameters are not */
    alg_s("begin procedure p(x); value x; integer x; x := 9; i := 1; p(i); outinteger(1, i) end",
          "1 ");
    alg_s("begin procedure p(x); integer x; x := 9; i := 1; p(i); outinteger(1, i) end", "9 ");
    alg_s("begin procedure p(x); integer x; x := 9; p(a[3]); outinteger(1, a[3]) end", "9 ");
    alg_s("begin procedure p(x); integer x; begin i := 2; x := 5 end; i := 1; p(a[i]); "
          "outinteger(1, a[1]); outinteger(1, a[2]) end",
          "0 5 ");
    alg_s("begin integer procedure twice(x); integer x; twice := x + x; i := 0; "
          "outinteger(1, twice(i + 3)) end",
          "6 ");
    alg_s("begin integer procedure g(x); integer x; begin i := i + 1; g := x end; i := 0; "
          "outinteger(1, g(i * 10)) end",
          "10 ");
    /* Jensen's device */
    alg_s("begin integer procedure sum(k, l, u, t); value l, u; integer k, l, u, t; "
          "begin integer s; s := 0; for k := l step 1 until u do s := s + t; sum := s end; "
          "outinteger(1, sum(i, 1, 10, i * i)); outinteger(1, sum(i, 1, 3, sum(j, 1, i, j))) end",
          "385 10 ");
    /* name parameters re-evaluate the subscript each time */
    alg_s("begin procedure swap(x, y); integer x, y; begin integer t; t := x; x := y; y := t "
          "end; i := 2; a[2] := 4; swap(i, a[i]); outinteger(1, i); outinteger(1, a[2]); "
          "outinteger(1, a[4]) end",
          "4 4 2 ");
    /* array parameters are passed by reference */
    alg_s("begin procedure fill(b, n); value n; integer array b; integer n; "
          "for k := 1 step 1 until n do b[k] := k * 2; fill(a, 10); outinteger(1, a[10]) end",
          "20 ");
    alg_s("begin integer procedure tot(b, l, u); value l, u; integer array b; integer l, u; "
          "begin integer s, k; s := 0; for k := l step 1 until u do s := s + b[k]; tot := s end; "
          "for i := 1 step 1 until 10 do a[i] := i; outinteger(1, tot(a, 1, 10)); "
          "begin integer array c[-1:1]; c[-1] := 5; c[1] := 6; outinteger(1, tot(c, -1, 1)) end "
          "end",
          "55 11 ");
    alg_s("begin procedure p(b); integer array b; b[2] := 1; procedure q(c); integer array c; "
          "p(c); q(a); outinteger(1, a[2]) end",
          "1 ");
    /* nested procedures reach their enclosing frames */
    alg_s("begin procedure outer(x); value x; integer x; begin integer y; "
          "procedure inner; begin y := y + x; i := i + y end; y := 1; inner; inner end; "
          "i := 0; outer(10); outinteger(1, i) end",
          "32 ");
    alg_s("begin integer procedure f(n); value n; integer n; begin integer procedure g(m); "
          "value m; integer m; g := if m = 0 then n else g(m - 1) + 1; f := g(n) end; "
          "outinteger(1, f(5)) end",
          "10 ");
    alg_s("begin procedure p(d); value d; integer d; begin integer x; procedure q; "
          "outinteger(1, x); x := d; if d > 0 then p(d - 1); q end; p(2) end",
          "0 1 2 ");
    /* forward references among the procedures of one block */
    alg_s("begin Boolean procedure ev(n); value n; integer n; "
          "ev := if n = 0 then true else od(n - 1); "
          "Boolean procedure od(n); value n; integer n; "
          "od := if n = 0 then false else ev(n - 1); "
          "outinteger(1, if ev(10) & od(7) then 1 else 0) end",
          "1 ");
    /* dynamic arrays in recursive procedures */
    alg_s("begin integer procedure tri(n); value n; integer n; begin integer array t[0:n]; "
          "integer m; t[0] := 0; for m := 1 step 1 until n do t[m] := t[m - 1] + m; "
          "tri := if n > 1 then t[n] + tri(n - 1) else t[n] end; outinteger(1, tri(70)) end",
          "59640 ");
    /* a global array changed by a name parameter thunk */
    alg_s("begin integer procedure ix(v); integer v; begin v := v + 1; ix := v end; i := 0; "
          "a[ix(i)] := 7; outinteger(1, a[1]); outinteger(1, i) end",
          "7 1 ");
    /* recursion with goto inside one activation */
    alg_s("begin integer procedure gcd(x, y); value x, y; integer x, y; begin "
          "L: if y != 0 then begin integer t; t := y; y := x - x % y * y; x := t; goto L end; "
          "gcd := x end; outinteger(1, gcd(1071, 462)) end",
          "21 ");
    /* a procedure statement with a typed procedure discards the value */
    alg_s("begin integer procedure f; begin i := i + 1; f := i end; f; f; outinteger(1, i) end",
          "2 ");
}

static void test_algol_traps(void)
{
    alg_trap("i := 1 % 0", ZL_E_DIVZERO, "% by zero");
    alg_trap("i := 1 / 0", ZL_E_DIVZERO, "/ by zero");
    alg_trap("i := 7 / 2", ZL_E_INEXACT, "/ with a remainder");
    alg_trap("i := 6 / 4 * 2", ZL_E_INEXACT, "each quotient must be exact");
    alg_trap("i := entier(1 / 3)", ZL_E_INEXACT, "entier does not round an inexact quotient");
    alg_trap("i := 2 ^ (-1)", ZL_E_INEXACT, "negative power");
    alg_trap("i := 0 ^ 0", ZL_E_DOMAIN, "0 ^ 0");
    alg_trap("i := 0 ^ (-1)", ZL_E_DOMAIN, "0 ^ -1");
    alg_trap("i := 2 ^ 63", ZL_E_OVERFLOW, "2 ^ 63");
    alg_trap("i := 9223372036854775807 + 1", ZL_E_OVERFLOW, "max + 1");
    alg_trap("i := -9223372036854775807 - 2", ZL_E_OVERFLOW, "min - 1");
    alg_trap("i := 3037000500 * 3037000500", ZL_E_OVERFLOW, "product");
    alg_trap("a[0] := 1", ZL_E_BOUNDS, "below a static array");
    alg_trap("a[11] := 1", ZL_E_BOUNDS, "above a static array");
    alg_trap("i := a[-5]", ZL_E_BOUNDS, "read below");
    alg_trap("n := 3; begin integer array b[1:n]; b[4] := 1 end", ZL_E_BOUNDS,
             "above a dynamic array");
    alg_trap("n := 3; begin integer array b[1:n]; i := b[0] end", ZL_E_BOUNDS,
             "below a dynamic array");
    alg_trap("n := 0; begin integer array b[1:n]; b[1] := 0 end", ZL_E_BOUNDS, "an empty array");
    alg_trap("n := 100000; begin integer array b[1:n]; b[1] := 0 end", ZL_E_RSTACK,
             "a dynamic array too big for the frames");
    alg_trap("begin procedure p(x); integer x; x := 1; p(i + 1) end", ZL_E_DOMAIN,
             "assignment to an expression passed by name");
    alg_trap("begin procedure p(x); integer x; x := 1; p(3) end", ZL_E_DOMAIN,
             "assignment to a constant passed by name");
    alg_trap("begin procedure r; r; r end", ZL_E_RSTACK, "unbounded recursion");
    alg_trap("begin integer procedure f(n); value n; integer n; f := f(n + 1); i := f(0) end",
             ZL_E_RSTACK, "unbounded typed recursion");
    alg_trap("L: goto L", ZL_E_FUEL, "an endless loop");
    alg_trap("for i := 1 step 0 until 2 do ;", ZL_E_FUEL, "a zero step");
    alg_trap("for i := 1 while true do ;", ZL_E_FUEL, "while true");
    alg_trap("outinteger(1, 1); i := 1 % 0", ZL_E_DIVZERO, "output before a trap");
    alg_trap("for i := 9223372036854775806 step 1 until 9223372036854775807 do ;", ZL_E_OVERFLOW,
             "stepping past the largest integer");
    CHECK(strcmp(out, "") == 0, "nothing printed: [%s]", out);
    expect_trap(AL, "begin\n  integer i;\n  i := 1;\n  i := i % (i - 1)\nend\n", ZL_E_DIVZERO, 4,
                "trap line");
    expect_trap(AL,
                "begin\n  integer array a[1:2];\n  procedure p(k);\n    value k; integer k;\n"
                "    a[k] := 1;\n  p(1);\n  p(3)\nend\n",
                ZL_E_BOUNDS, 5, "trap line inside a procedure");
    {
        int s = run_src(AL, "begin integer i; for i := 1 step 1 until 1000 do ; end", 0, 0, 50);
        CHECK(s == ZL_E_FUEL, "fuel runs out: %d", s);
    }
}

static void test_algol_refused(void)
{
    /* outside the subset, by name */
    alg_ref("real x;", ZL_E_UNSUPPORTED, "real");
    alg_ref("i := 1.5;", ZL_E_UNSUPPORTED, "a real number");
    alg_ref("i := 1e3;", ZL_E_UNSUPPORTED, "an exponent part");
    alg_ref("own integer x;", ZL_E_UNSUPPORTED, "own");
    alg_ref("switch s := L1, L2;", ZL_E_UNSUPPORTED, "switch");
    alg_ref("string s;", ZL_E_UNSUPPORTED, "string");
    alg_ref("real array r[1:2];", ZL_E_UNSUPPORTED, "real array");
    alg_ref("array r[1:2];", ZL_E_UNSUPPORTED, "plain array");
    alg_ref("integer array m[1:2, 1:2];", ZL_E_UNSUPPORTED, "two dimensions");
    alg_ref("real procedure f; f := 1;", ZL_E_UNSUPPORTED, "real procedure");
    alg_ref("procedure p(l); label l; goto l;", ZL_E_UNSUPPORTED, "label parameter");
    alg_ref("procedure p(f); procedure f; f;", ZL_E_UNSUPPORTED, "procedure parameter");
    alg_ref("procedure p(s); string s; ;", ZL_E_UNSUPPORTED, "string parameter");
    alg_ref("procedure p(b); value b; integer array b; ;", ZL_E_UNSUPPORTED, "value array");
    alg_ref("procedure p(x); real x; ;", ZL_E_UNSUPPORTED, "real parameter");
    alg_ref("procedure p; code;", ZL_E_UNSUPPORTED, "code body");
    alg_ref("10: i := 1;", ZL_E_UNSUPPORTED, "numeric label");
    alg_ref("i := sqrt(4);", ZL_E_UNSUPPORTED, "sqrt");
    alg_ref("i := sin(0);", ZL_E_UNSUPPORTED, "sin");
    alg_ref("i := ln(1);", ZL_E_UNSUPPORTED, "ln");
    alg_ref("i := exp(0);", ZL_E_UNSUPPORTED, "exp");
    alg_ref("i := arctan(0);", ZL_E_UNSUPPORTED, "arctan");
    alg_ref("i := cos(0);", ZL_E_UNSUPPORTED, "cos");
    alg_ref("outreal(1, 1);", ZL_E_UNSUPPORTED, "outreal");
    alg_ref("inreal(0, i);", ZL_E_UNSUPPORTED, "inreal");
    alg_ref("i := maxreal;", ZL_E_UNSUPPORTED, "maxreal");
    alg_ref("outinteger(2, 1);", ZL_E_UNSUPPORTED, "another channel");
    alg_ref("outstring(0, \"x\");", ZL_E_UNSUPPORTED, "output on the input channel");
    alg_ref("ininteger(1, i);", ZL_E_UNSUPPORTED, "input from the output channel");
    alg_ref("for a[1] := 1 step 1 until 2 do ;", ZL_E_UNSUPPORTED, "subscripted control");
    alg_ref("goto if i = 1 then L else M; L: M: ;", ZL_E_UNSUPPORTED,
            "conditional designational expression");
    alg_ref("procedure p; goto L; L: ;", ZL_E_UNSUPPORTED, "a label outside the procedure");
    alg_ref("goto L; begin integer x; L: x := 1 end;", ZL_E_UNDEFINED, "goto into a block");
    alg_ref("i := 1 end trailing words; begin", ZL_E_UNSUPPORTED, "an end comment");
    alg_ref("i := 1 / 2 % 2;", ZL_E_SYNTAX, "% of a real quotient");
    alg_ref("i := abs(4) % 2;", ZL_E_SYNTAX, "% of abs, which is real");
    expect_refused(AL,
                   "begin\n  integer n;\n  n := 1;\n  L: begin\n    integer array b[1:n];\n"
                   "    goto L\n  end\nend\n",
                   ZL_E_UNSUPPORTED, 6, "goto out of a block with a dynamic array");
    expect_refused(AL,
                   "begin\n  integer x;\n  procedure p;\n  begin\n    goto L\n  end;\n"
                   "  L: x := 1\nend\n",
                   ZL_E_UNSUPPORTED, 5, "goto out of a procedure");
    /* syntax and meaning */
    alg_ref("i := ;", ZL_E_SYNTAX, "missing expression");
    alg_ref("i = 1;", ZL_E_SYNTAX, "= is not assignment");
    alg_ref("i := 7 % -2;", ZL_E_SYNTAX, "a sign after an operator");
    alg_ref("i := (1 + 2;", ZL_E_SYNTAX, "unclosed parenthesis");
    alg_ref("i := 1 + if true then 1 else 2;", ZL_E_SYNTAX, "unparenthesised conditional");
    alg_ref("if true then if true then i := 1;", ZL_E_SYNTAX, "if after then");
    alg_ref("i := if true then if true then 1 else 2 else 3;", ZL_E_SYNTAX,
            "conditional after then");
    alg_ref("i := true;", ZL_E_SYNTAX, "Boolean into integer");
    alg_ref("i := 1 & 2;", ZL_E_SYNTAX, "& on integers");
    alg_ref("if 1 then i := 1;", ZL_E_SYNTAX, "integer condition");
    alg_ref("i := !1;", ZL_E_SYNTAX, "! on an integer");
    alg_ref("i := (1 < 2) + 1;", ZL_E_SYNTAX, "+ on a Boolean");
    alg_ref("i := 1 < 2 < 3;", ZL_E_SYNTAX, "chained relations");
    alg_ref("k := 1;", ZL_E_UNDEFINED, "undeclared variable");
    alg_ref("i := a;", ZL_E_SYNTAX, "array without subscript");
    alg_ref("i := i[1];", ZL_E_SYNTAX, "subscripted simple variable");
    alg_ref("goto nowhere;", ZL_E_UNDEFINED, "missing label");
    alg_ref("L: L: ;", ZL_E_SYNTAX, "label twice");
    alg_ref("i := 1; integer k;", ZL_E_SYNTAX, "declaration after a statement");
    alg_ref("begin integer x, x; end;", ZL_E_SYNTAX, "declared twice");
    alg_ref("p(1);", ZL_E_UNDEFINED, "undeclared procedure");
    alg_ref("begin procedure p(x); value x; integer x; ; p end;", ZL_E_SYNTAX, "too few arguments");
    alg_ref("begin procedure p(x); value x; integer x; ; p(1, 2) end;", ZL_E_SYNTAX,
            "too many arguments");
    alg_ref("begin procedure p(x); value x; integer x; ; p(a) end;", ZL_E_SYNTAX,
            "array for an integer");
    alg_ref("begin procedure p(b); integer array b; ; p(i) end;", ZL_E_SYNTAX,
            "integer for an array");
    alg_ref("begin procedure p(x); integer x; ; i := p(1) end;", ZL_E_SYNTAX,
            "untyped procedure in an expression");
    alg_ref("begin procedure p(x); value y; integer x; ; end;", ZL_E_SYNTAX,
            "value of a non-parameter");
    alg_ref("begin procedure p(x); ; end;", ZL_E_SYNTAX, "parameter without a type");
    alg_ref("outinteger(1);", ZL_E_SYNTAX, "outinteger arity");
    alg_ref("outstring(1, 2);", ZL_E_SYNTAX, "outstring wants a string");
    alg_ref("ininteger(0, 1);", ZL_E_SYNTAX, "ininteger wants a variable");
    alg_ref("i := \"x\";", ZL_E_SYNTAX, "a string in an expression");
    alg_ref("i := 1 $ 2;", ZL_E_SYNTAX, "a stray character");
    alg_ref("i := 99999999999999999999;", ZL_E_UNSUPPORTED, "a number too large");
    alg_ref("outstring(1, \"abc);", ZL_E_SYNTAX, "unterminated string");
    alg_ref("for i := 1 step 1 do ;", ZL_E_SYNTAX, "step without until");
    alg_ref("for p := 1 do ;", ZL_E_UNDEFINED, "undeclared control");
    alg_ref("i := 1 i := 2;", ZL_E_SYNTAX, "missing semicolon");
    expect_refused(AL, "", ZL_E_SYNTAX, 0, "empty program");
    expect_refused(AL, "begin end end", ZL_E_SYNTAX, 1, "text after the program");
    expect_refused(AL, "begin integer i; i := 1", ZL_E_SYNTAX, 1, "missing end");
    expect_refused(AL, "comment only;", ZL_E_SYNTAX, 1, "no program");
    expect_refused(AL, "begin integer i; Boolean b; b := 1 < 2; i := b end", ZL_E_SYNTAX, 1,
                   "Boolean variable into integer");
    expect_refused(AL, "begin\n  integer i;\n\n  i := 1 +\nend\n", ZL_E_SYNTAX, 5,
                   "error on the line it is found");
}

static void test_algol_twin(void)
{
    expect_twin(AL, "begin integer i; i:=1; outinteger(1,i) end",
                "begin\n  integer i;\n  i := 1;\n  outinteger(1, i)\nend\n");
    expect_twin(AL, "begin integer i, j; for i:=1 step 1 until 3 do j:=j+i end",
                "begin\n  integer i, j;\n  for i := 1 step 1 until 3 do\n    j := j + i\nend\n");
    expect_twin(AL,
                "begin integer i; if i>2 then begin i:=0; goto L end else i:=1; "
                "L: outstring(1,\"x\") end",
                "begin\n  integer i;\n  if i > 2 then begin\n    i := 0;\n    goto L\n  end else\n"
                "    i := 1;\n  L: outstring(1, \"x\")\nend\n");
    expect_twin(AL, "begin integer i; i := ((1 + 2)) * (3) - (4 - 5) + (-6) end",
                "begin\n  integer i;\n  i := (1 + 2) * 3 - (4 - 5) + (-6)\nend\n");
    expect_twin(AL, "begin integer i; i := 7 div 2 ** 3 \xc3\x97 1; go to L; L: end",
                "begin\n  integer i;\n  i := 7 % 2 ^ 3 * 1;\n  goto L;\n  L:\nend\n");
    expect_twin(AL, "begin Boolean p; p := \xc2\xac p \xe2\x88\xa7 true \xe2\x8a\x83 false end",
                "begin\n  Boolean p;\n  p := !p & true -> false\nend\n");
    expect_twin(AL, "begin integer i; ;; i := 1;;; end", "begin\n  integer i;\n  i := 1\nend\n");
    expect_twin(AL, "begin comment  two   spaces\n here; integer i; i := 1 end",
                "begin\n  comment two spaces here;\n  integer i;\n  i := 1\nend\n");
    expect_twin(AL, "begin integer i; i := 1 - (2 - 3) - 4 + (5 + 6) end",
                "begin\n  integer i;\n  i := 1 - (2 - 3) - 4 + (5 + 6)\nend\n");
    expect_twin(AL, "begin integer i; i := 2 ^ (3 ^ 2) + (2 ^ 3) ^ 2 end",
                "begin\n  integer i;\n  i := 2 ^ (3 ^ 2) + 2 ^ 3 ^ 2\nend\n");
    expect_twin(AL, "begin integer i; i := -(2 ^ 2) + (-2) ^ 2 end",
                "begin\n  integer i;\n  i := -2 ^ 2 + (-2) ^ 2\nend\n");
    expect_twin(AL, "begin integer i; i := (if true then 1 else 2) end",
                "begin\n  integer i;\n  i := if true then 1 else 2\nend\n");
    expect_twin(AL,
                "begin integer procedure f(x); value x; integer x; begin f := x end; "
                "outinteger(1, f(2)) end",
                "begin\n  integer procedure f(x); value x; integer x;\n  begin\n    f := x\n  end;"
                "\n  outinteger(1, f(2))\nend\n");
    expect_twin(AL, "begin procedure p; outinteger(1, 1); p end",
                "begin\n  procedure p;\n    outinteger(1, 1);\n  p\nend\n");
    expect_twin(AL, "begin integer i; for i := 1, 2 step 1 until 3, 4 while false do ; end",
                "begin\n  integer i;\n  for i := 1, 2 step 1 until 3, 4 while false do\nend\n");
    expect_twin(AL, "begin integer array a[1:2], b[0:1]; Boolean array c[1:1]; a[1] := b[0] end",
                "begin\n  integer array a[1:2], b[0:1];\n  Boolean array c[1:1];\n"
                "  a[1] := b[0]\nend\n");
    /* the twin is canonical: anything else is refused in twin mode */
    {
        zl_diag_t d;
        const char *t = "begin\n  integer i;\n  i := 1\nend\n";
        CHECK(zl_compile(AL | ZL_TWIN, t, strlen(t), &P, &work, &d) == ZL_OK,
              "canonical twin accepted: %s", d.msg);
        const char *u = "begin\n  integer i;\n  i:=1\nend\n";
        CHECK(zl_compile(AL | ZL_TWIN, u, strlen(u), &P, &work, &d) == ZL_E_NONCANON && d.line == 3,
              "non-canonical twin refused on its line: %u", d.line);
        const char *v = "begin\n  integer i;\n  i := 1\nend";
        CHECK(zl_compile(AL | ZL_TWIN, v, strlen(v), &P, &work, &d) == ZL_E_NONCANON,
              "twin without its final newline refused");
    }
}

/* Many small mutations of a working program: every accepted one must keep
 * the twin properties; none may crash. */
static void test_algol_mutants(void)
{
    static const char base[] =
        "begin integer i, s; integer array a[1:5];\n"
        "  integer procedure f(x, y); value x; integer x, y;\n"
        "    f := if x > 0 then x * y + f(x - 1, y) else 0;\n"
        "  for i := 1 step 1 until 5 do a[i] := i * i;\n"
        "  s := 0; for i := 1 step 1 until 5 do s := s + a[i] % 2;\n"
        "  outinteger(1, s); outinteger(1, f(3, s)); outstring(1, \"ok\\n\")\n"
        "end\n";
    static char m[sizeof base + 8];
    static const char subst[] = " ;:=()[]+-*/%^<>!&|0123456789aeifx\"\n";
    size_t len = strlen(base);
    int ok = 0, tried = 0;
    expect_out(AL, base, "26 156 ok\n", "mutant base");
    for (size_t pos = 0; pos < len; pos += 3) {
        for (size_t c = 0; c < sizeof subst - 1; c += 5) {
            memcpy(m, base, len + 1);
            m[pos] = subst[c];
            zl_diag_t d;
            int s = zl_compile(AL, m, len, &P, &work, &d);
            tried++;
            if (s == ZL_OK) {
                ok++;
                run_p(&P, 0, 0, 20000);
                twin_props(AL, m, len, "mutant");
            } else
                CHECK(s < 0 && d.status == s && d.msg && d.msg[0] && d.line >= 1 && d.line <= 8,
                      "mutant refusal at %zu is reported (%d line %u)", pos, s, d.line);
        }
    }
    CHECK(ok > 10 && tried > 500, "mutants: %d of %d accepted", ok, tried);
}

static void test_algol_limits(void)
{
    static char src[1 << 15];
    size_t n = 0;
    /* nesting beyond ZL_MAX_DEPTH is refused, not a crash */
    n += (size_t) snprintf(src + n, sizeof src - n, "begin integer i; i := ");
    for (int k = 0; k < ZL_MAX_DEPTH + 4; k++) src[n++] = '(';
    src[n++] = '1';
    for (int k = 0; k < ZL_MAX_DEPTH + 4; k++) src[n++] = ')';
    n += (size_t) snprintf(src + n, sizeof src - n, " end");
    expect_refused(AL, src, ZL_E_LIMIT, 1, "deep parentheses");
    n = 0;
    for (int k = 0; k < ZL_MAX_DEPTH + 4; k++)
        n += (size_t) snprintf(src + n, sizeof src - n, "begin ");
    for (int k = 0; k < ZL_MAX_DEPTH + 4; k++)
        n += (size_t) snprintf(src + n, sizeof src - n, "end ");
    expect_refused(AL, src, ZL_E_LIMIT, 1, "deep blocks");
    /* nesting just inside the limit works */
    n = 0;
    n += (size_t) snprintf(src + n, sizeof src - n, "begin outinteger(1, ");
    for (int k = 0; k < 20; k++) src[n++] = '(';
    src[n++] = '3';
    for (int k = 0; k < 20; k++) src[n++] = ')';
    n += (size_t) snprintf(src + n, sizeof src - n, ") end");
    src[n] = 0;
    expect_out(AL, src, "3 ", "20 parentheses");
    /* too many slots */
    expect_refused(AL, "begin integer array a[1:5000]; a[1] := 1 end", ZL_E_LIMIT, 1,
                   "array larger than memory");
    /* many variables */
    n = 0;
    n += (size_t) snprintf(src + n, sizeof src - n, "begin integer v0");
    for (int k = 1; k < 300; k++) n += (size_t) snprintf(src + n, sizeof src - n, ", v%d", k);
    n += (size_t) snprintf(src + n, sizeof src - n, ";\n v299 := 5; outinteger(1, v299) end\n");
    expect_out(AL, src, "5 ", "300 variables");
    /* a long program of statements */
    n = 0;
    n += (size_t) snprintf(src + n, sizeof src - n, "begin integer i;\n");
    for (int k = 0; k < 400; k++) n += (size_t) snprintf(src + n, sizeof src - n, "i := i + 1;\n");
    n += (size_t) snprintf(src + n, sizeof src - n, "outinteger(1, i)\nend\n");
    expect_out(AL, src, "400 ", "400 statements");
}

static void test_algol(void)
{
    zl_diag_t d;
    CHECK(strcmp(zl_lang_name(AL), "ALGOL 60") == 0, "language name");
    CHECK(compile(AL, "begin end", &d) == ZL_OK && P.lang == AL, "smallest program");
    test_algol_expr();
    test_algol_stmt();
    test_algol_proc();
    test_algol_traps();
    test_algol_refused();
    test_algol_twin();
    test_algol_mutants();
    test_algol_limits();
}

#undef AL
