/* test_zl_pascal.inc — Pascal (Tutordrake) front-end tests, included by test_zl.c.
 * Expected outputs are what fpc 3.2 -Miso prints for the same program. */

/* program t(output); [decls] begin <body> end.  The body starts on line 3
 * without declarations; PD's declaration is line 2 and its body line 4. */
#define PS(body)       "program t(output);\nbegin\n" body "\nend.\n"
#define PD(decl, body) "program t(output);\n" decl "\nbegin\n" body "\nend.\n"

static void pas_in(const char *src, const int64_t *in, size_t nin, int want_status,
                   const char *want, const char *what)
{
    int s = run_src(ZL_LANG_PASCAL, src, in, nin, 0);
    CHECK(s == want_status, "%s: status %d want %d (out [%s])", what, s, want_status, out);
    if (want) CHECK(strcmp(out, want) == 0, "%s: out [%s] want [%s]", what, out, want);
}

static void test_pascal_output(void)
{
    const int L = ZL_LANG_PASCAL;
    expect_out(L, PS("writeln(1)"), "          1\n", "integers take 11 columns");
    expect_out(L, PS("writeln(-2147483647, 2147483647)"), "-2147483647 2147483647\n",
               "the widest integers fill the 11 columns");
    expect_out(L, PS("write(5:4, -5:4, 123:1, 7:0, 12345:3)"), "   5  -5123712345",
               "integer widths pad but never cut");
    expect_out(L, PS("write(1:64)"),
               "                                                               1",
               "width 64 is the most");
    expect_out(L, PS("writeln('it''s', 'a':3, 'abc':2, 'abc':0, '|')"), "it's  aab|\n",
               "strings pad on the left and are cut to their width");
    expect_out(L, PS("writeln(true, false, true:6, false:2, true:0, '|')"), " truefalse  truefa|\n",
               "Booleans take 5 columns and are cut to a width");
    expect_out(L, PS("writeln"), "\n", "writeln alone");
    expect_out(L, PS("write('a'); write('b'); writeln; writeln('c')"), "ab\nc\n",
               "write leaves the line open");
    expect_out(L, PD("const w = 4; n = 0;", "writeln(7:w, 'x':w, true:w, 8:n)"), "   7   xtrue8\n",
               "constant widths");
    expect_out(L, PS("writeln('')"), "\n", "an empty string");
    expect_out(L, PS("writeln(1 = 1, 1 < 1)"), " truefalse\n", "Boolean results print");
}

static void test_pascal_arith(void)
{
    const int L = ZL_LANG_PASCAL;
    expect_out(L,
               PS("writeln(2 + 3 * 4:1, ' ', (2 + 3) * 4:1, ' ', 10 - 4 - 3:1, ' ', "
                  "100 div 10 div 2:1, ' ', 2 * 3 mod 4:1)"),
               "14 20 3 5 2\n", "precedence and left association");
    expect_out(L, PS("writeln(-2 * 3 + 1:1, ' ', -7 mod 3:1, ' ', (-7) mod 3:1, ' ', - 2 - 3:1)"),
               "-5 -1 2 -5\n", "a leading sign applies to the first term");
    expect_out(L,
               PS("writeln(7 div 2:1, ' ', (-7) div 2:1, ' ', 7 div (-2):1, ' ', (-7) div (-2):1, "
                  "' ', 0 div 5:1)"),
               "3 -3 -3 3 0\n", "div truncates toward zero");
    expect_out(L,
               PS("writeln(7 mod 3:1, ' ', (-7) mod 3:1, ' ', (-6) mod 3:1, ' ', 0 mod 5:1, ' ', "
                  "(-1) mod 7:1, ' ', 5 mod 1:1)"),
               "1 2 0 0 6 0\n", "mod is never negative (ISO 7185)");
    expect_out(L, PS("writeln(abs(-4):1, abs(4):2, sqr(-3):2, sqr(0):2, succ(4):2, pred(4):2)"),
               "4 4 9 0 5 3\n", "abs sqr succ pred");
    expect_out(L, PS("writeln(odd(3), odd(-3), odd(0), odd(-4), odd(maxint))"),
               " true truefalsefalse true\n", "odd");
    expect_out(L, PS("writeln(ord(true):2, ord(false):2, ord(-5):3, ord(7):2)"), " 1 0 -5 7\n",
               "ord");
    expect_out(L, PS("writeln(maxint, -maxint - 1)"), " 2147483647-2147483648\n",
               "maxint, and the most negative integer");
    expect_out(L, PS("writeln(1 < 2, 2 <= 2, 3 > 4, 4 >= 5, 5 = 5, 6 <> 6)"),
               " true truefalsefalse truefalse\n", "comparisons");
    expect_out(L, PS("writeln(false < true, true = true, false >= true, true <> false)"),
               " true truefalse true\n", "Booleans compare false < true");
    expect_out(L,
               PS("writeln(true and false, true or false, not true, not not true, "
                  "not (1 > 2) and (2 > 1))"),
               "false truefalse true true\n", "and or not");
    expect_out(L,
               PD("var z: integer;", "z := 0;\nwriteln((z > 0) and (1 div z > 0), "
                                     "(z = 0) or (1 mod z = 0))"),
               "false true\n", "and and or stop early, as fpc does");
    expect_out(L, PS("writeln(+5:1, ' ', -(-5):1, ' ', -(2 - 3):1)"), "5 5 1\n",
               "unary plus and nested signs");
    expect_out(L, PS("writeln(maxint - 1 + 1 = maxint)"), " true\n",
               "results inside 32 bits are fine");
    expect_trap(L, PS("writeln(maxint + 1)"), ZL_E_OVERFLOW, 3, "maxint + 1");
    expect_trap(L, PS("writeln(-maxint - 2)"), ZL_E_OVERFLOW, 3, "below 32 bits");
    expect_trap(L, PS("writeln(65536 * 32768)"), ZL_E_OVERFLOW, 3, "product past 32 bits");
    expect_trap(L, PS("writeln(sqr(46341))"), ZL_E_OVERFLOW, 3, "sqr past 32 bits");
    expect_out(L, PS("writeln(sqr(46340):1)"), "2147395600\n", "the largest sqr");
    expect_trap(L, PS("writeln(succ(maxint))"), ZL_E_OVERFLOW, 3, "succ(maxint)");
    expect_trap(L, PS("writeln(pred(-maxint - 1))"), ZL_E_OVERFLOW, 3, "pred of the least");
    expect_trap(L, PS("writeln(abs(-maxint - 1))"), ZL_E_OVERFLOW, 3, "abs of the least");
    expect_trap(L, PS("writeln(-(-maxint - 1))"), ZL_E_OVERFLOW, 3, "negating the least");
    expect_trap(L, PS("writeln((-maxint - 1) div (-1))"), ZL_E_OVERFLOW, 3, "least div -1");
    expect_trap(L, PD("var z: integer;", "z := 0;\nwriteln(1 div z)"), ZL_E_DIVZERO, 5,
                "div by zero");
    expect_trap(L, PD("var z: integer;", "z := 0;\nwriteln(1 mod z)"), ZL_E_DIVZERO, 5,
                "mod by zero");
    expect_trap(L, PD("var z: integer;", "z := -2;\nwriteln(7 mod z)"), ZL_E_DOMAIN, 5,
                "mod by a negative number is an error in ISO 7185");
    expect_trap(L, PS("writeln(7 mod (-2))"), ZL_E_DOMAIN, 3, "mod by a negative constant");
    expect_out(L, PD("var z: integer;", "z := 7;\nwriteln((-maxint - 1) mod z:1)"), "5\n",
               "mod of the least integer");
}

static void test_pascal_statements(void)
{
    const int L = ZL_LANG_PASCAL;
    expect_out(L,
               PD("var i: integer;",
                  "i := 3;\nif i > 2 then writeln('big') else writeln('small');\n"
                  "if i > 5 then writeln('huge') else writeln('not huge');\n"
                  "if i = 3 then writeln('three')"),
               "big\nnot huge\nthree\n", "if then else");
    expect_out(L, PS("if true then if false then writeln('a') else writeln('b')"), "b\n",
               "else binds to the nearest if");
    expect_out(L, PS("if false then if true then writeln('a') else writeln('b')"), "",
               "dangling else under a false outer if");
    expect_out(L,
               PD("var i: integer;", "i := 2;\nif i = 1 then writeln('one') else if i = 2 then "
                                     "writeln('two') else writeln('many')"),
               "two\n", "else if");
    expect_out(L,
               PD("var i, s: integer;",
                  "i := 0; s := 0;\nwhile i < 10 do begin i := i + 1; s := s + i end;\n"
                  "writeln(s:1)"),
               "55\n", "while");
    expect_out(L, PS("while false do writeln('never')"), "", "while that never runs");
    expect_out(L, PD("var i: integer;", "i := 0;\nrepeat i := i + 2; write(i:2) until i >= 6"),
               " 2 4 6", "repeat runs its body first");
    expect_out(L, PS("repeat writeln('once') until true"), "once\n", "repeat at least once");
    expect_out(L,
               PD("var i: integer;", "for i := 1 to 4 do write(i:2);\nfor i := 4 downto 1 do "
                                     "write(i:2)"),
               " 1 2 3 4 4 3 2 1", "for to and downto");
    expect_out(L,
               PD("var i: integer;", "for i := 2 to 1 do write('x');\nfor i := 1 downto 2 do "
                                     "write('y');\nwriteln('|')"),
               "|\n", "for loops that run zero times");
    expect_out(L, PD("var i: integer;", "for i := 5 to 5 do write(i:1)"), "5", "one iteration");
    expect_out(L, PD("var i: integer;", "for i := maxint - 2 to maxint do write(i - maxint:3)"),
               " -2 -1  0", "for up to maxint does not overflow");
    expect_out(L,
               PD("var i: integer;", "for i := -maxint downto -maxint - 1 do write(i + maxint:3)"),
               "  0 -1", "for down to the least integer does not overflow");
    expect_out(L, PD("var i, n: integer;", "n := 3;\nfor i := 1 to n do n := n + 1;\nwriteln(n:1)"),
               "6\n", "the limit is evaluated once");
    expect_out(L,
               PD("var i, j, s: integer;",
                  "s := 0;\nfor i := 1 to 3 do for j := i to 3 do s := s + 10 * i + j;\n"
                  "writeln(s:1)"),
               "114\n", "nested for");
    expect_out(L,
               PD("var i: integer;",
                  "for i := -1 to 4 do\ncase i of\n0: write('a');\n1, 2: write('b');\n"
                  "-1: write('m');\n4, 3: begin write('c'); write('d') end;\nend"),
               "mabbcdcd", "case with lists, negative labels and a trailing ';'");
    expect_out(L, PD("const k = 7; m = -k;", "case -7 of k: write('k'); m: write('m') end"), "m",
               "case labels may be constants");
    expect_trap(L, PD("var i: integer;", "i := 5;\ncase i of 1: write('a'); 2: write('b') end"),
                ZL_E_DOMAIN, 5, "a case without a matching label traps");
    expect_out(L, PS("begin end"), "", "an empty compound");
    expect_out(L, PS(";;; writeln('x');;"), "x\n", "empty statements");
    expect_out(L, PS("if true then else writeln('no');\nwriteln('ok')"), "ok\n", "empty branch");
    expect_out(L, PS("BEGIN WriteLn('Mixed') END"), "Mixed\n", "keywords in any case");
    expect_out(L, PD("var Abc: integer;", "aBC := 4;\nwriteln(ABC:1)"), "4\n",
               "names in any case are one name");
}

static void test_pascal_procs(void)
{
    const int L = ZL_LANG_PASCAL;
    expect_out(L,
               PD("var a: integer;\nprocedure p(x: integer);\nbegin x := x + 1; write(x:2) end;",
                  "a := 1;\np(a);\nwriteln(a:2)"),
               " 2 1\n", "value parameters are copies");
    expect_out(L,
               PD("var a: integer;\nprocedure p(var x: integer);\nbegin x := x + 1 end;",
                  "a := 1;\np(a); p(a);\nwriteln(a:2)"),
               " 3\n", "var parameters reach the caller's variable");
    expect_out(L,
               PD("var a, b: integer;\nprocedure swap(var x, y: integer);\nvar t: integer;\n"
                  "begin t := x; x := y; y := t end;",
                  "a := 1; b := 2;\nswap(a, b);\nwriteln(a:2, b:2)"),
               " 2 1\n", "swap");
    expect_out(L,
               PD("var v: array[1..3] of integer;\nprocedure inc(var x: integer);\n"
                  "begin x := x * 10 end;",
                  "v[2] := 4;\ninc(v[2]);\nwriteln(v[2]:3)"),
               " 40\n", "an array element as a var argument");
    expect_out(L,
               PD("var g: integer;\nprocedure outer(var x: integer);\n"
                  "  procedure inner(var y: integer);\n  begin y := y + 5 end;\n"
                  "begin inner(x); inner(x) end;",
                  "g := 1;\nouter(g);\nwriteln(g:3)"),
               " 11\n", "a var parameter passed on as var");
    expect_out(L,
               PD("procedure p;\nvar n: integer;\n  procedure bump;\n  begin n := n + 1 end;\n"
                  "begin n := 10; bump; bump; writeln(n:3) end;",
                  "p"),
               " 12\n", "a nested procedure changes its parent's local");
    expect_out(L,
               PD("var d: integer;\nprocedure a(n: integer);\n  procedure b;\n"
                  "    procedure c;\n    begin d := d + n end;\n  begin c end;\n"
                  "begin if n > 0 then begin b; a(n - 1); b end end;",
                  "d := 0;\na(3);\nwriteln(d:3)"),
               " 12\n", "static links follow the definition, not the call");
    expect_out(L,
               PD("function fact(n: integer): integer;\n"
                  "begin if n <= 1 then fact := 1 else fact := n * fact(n - 1) end;",
                  "writeln(fact(12):1)"),
               "479001600\n", "recursive function");
    expect_trap(L,
                PD("function fact(n: integer): integer;\n"
                   "begin if n <= 1 then fact := 1 else fact := n * fact(n - 1) end;",
                   "writeln(fact(13):1)"),
                ZL_E_OVERFLOW, 3, "13! leaves 32 bits");
    expect_out(L, PD("function seven: integer;\nbegin seven := 7 end;", "writeln(seven * 2:1)"),
               "14\n", "a function without parameters");
    expect_out(L, PD("function f: integer;\nbegin end;", "writeln(f:1)"), "0\n",
               "a result never set reads as zero");
    expect_out(L,
               PD("function even(n: integer): boolean;\nbegin even := n mod 2 = 0 end;",
                  "writeln(even(4), even(7))"),
               " truefalse\n", "Boolean function");
    expect_out(L,
               PD("function f(n: integer): integer;\n  procedure setit;\n  begin f := n * 2 end;\n"
                  "begin setit end;",
                  "writeln(f(21):1)"),
               "42\n", "a nested procedure sets its function's result");
    expect_out(L, PD("function f(f: integer): integer;\nbegin end;", "writeln(f(3):1)"), "0\n",
               "a parameter may share its function's name");
    expect_out(L,
               PD("procedure p(n: integer);\nvar a: array[0..2] of integer;\n"
                  "begin a[n mod 3] := n; if n > 0 then p(n - 1); write(a[n mod 3]:2) end;",
                  "p(4)"),
               " 0 1 2 3 4", "each call has its own local array");
    expect_out(L,
               PD("var x: integer;\nprocedure p;\nvar x: integer;\nbegin x := 5; write(x:2) end;",
                  "x := 1;\np;\nwriteln(x:2)"),
               " 5 1\n", "a local hides a global");
    expect_out(L,
               PD("var x: integer;\nprocedure p(x: integer);\nbegin write(x:2) end;",
                  "x := 1;\np(9);\nwriteln(x:2)"),
               " 9 1\n", "a parameter hides a global");
    expect_out(L,
               PD("var v: array[-2..2] of integer; i: integer;\n"
                  "procedure fill(var s: integer);\nvar w: array[-2..2] of integer; k: integer;\n"
                  "begin for k := -2 to 2 do w[k] := k * k; s := 0; for k := -2 to 2 do s := s + "
                  "w[k] end;",
                  "for i := -2 to 2 do v[i] := i;\nfill(i);\nwriteln(i:1, v[-2]:3, v[2]:2)"),
               "10 -2 2\n", "global and local arrays with a negative lower bound");
    expect_out(L,
               PD("var b: boolean; c: array[1..2] of boolean;\nprocedure flip(var x: boolean);\n"
                  "begin x := not x end;",
                  "flip(b); flip(c[2]);\nwriteln(b, c[1], c[2])"),
               " truefalse true\n", "Boolean var parameters");
    expect_out(L,
               PD("var n: integer;\nfunction f(var k: integer): integer;\n"
                  "begin k := k + 1; f := k * 10 end;",
                  "n := 1;\nwriteln(f(n) + n:1)"),
               "22\n", "a function changing a var argument, left to right");
    expect_out(L,
               PD("procedure a(n: integer);\n  function b(m: integer): integer;\n"
                  "  begin b := m + n end;\nbegin writeln(b(n * 10):1) end;",
                  "a(4)"),
               "44\n", "a nested function reads its parent's parameter");
    expect_trap(L, PD("procedure p;\nbegin p end;", "p"), ZL_E_RSTACK, 0,
                "endless recursion runs out of frames");
    expect_trap(L,
                PD("procedure p(n: integer);\nvar a: array[1..3] of integer;\nbegin a[n] := 1 end;",
                   "p(4)"),
                ZL_E_BOUNDS, 4, "a local array index past its bound");
    expect_trap(L,
                PD("procedure p(n: integer);\nvar a: array[1..3] of integer;\n"
                   "begin writeln(a[n]) end;",
                   "p(0)"),
                ZL_E_BOUNDS, 4, "a local array index below its bound");
    expect_trap(L, PD("var a: array[1..3] of integer;", "a[4] := 1"), ZL_E_BOUNDS, 4,
                "a global array index past its bound");
    expect_trap(L, PD("var a: array[-3..-1] of integer;", "writeln(a[0])"), ZL_E_BOUNDS, 4,
                "a global array index past a negative bound");
    expect_trap(
        L,
        PD("var a: array[1..3] of integer;\nprocedure p(var x: integer);\nbegin end;", "p(a[5])"),
        ZL_E_BOUNDS, 0, "a var argument outside its array");
    expect_out(L,
               PD("var a, b: integer; c: boolean; v: array[1..2] of integer;",
                  "writeln(a:2, b:2, c, v[1]:2)"),
               " 0 0false 0\n", "variables start at zero");
}

static void test_pascal_read(void)
{
    const int L = ZL_LANG_PASCAL;
    int64_t in[4] = {6, -7, 8, 2147483647};
    (void) L;
    pas_in(PD("var a, b: integer;", "read(a, b);\nwriteln(a * b:1)"), in, 2, ZL_OK, "-42\n",
           "read takes the input list in order");
    pas_in(PD("var a: integer; v: array[1..3] of integer;",
              "readln(v[2]);\nreadln;\nread(a);\nwriteln(v[2] + a:1)"),
           in, 3, ZL_OK, "-1\n", "readln and array elements");
    pas_in(PD("var a: integer;\nprocedure get(var x: integer);\nbegin read(x) end;",
              "get(a);\nwriteln(a:1)"),
           in, 1, ZL_OK, "6\n", "read into a var parameter");
    pas_in(PD("var a: integer;", "read(a);\nread(a)"), in, 1, ZL_E_INPUT, "", "input exhausted");
    {
        int64_t big[1] = {(int64_t) 1 << 31};
        pas_in(PD("var a: integer;", "read(a)"), big, 1, ZL_E_OVERFLOW, "",
               "an input value outside 32 bits");
    }
    pas_in(PD("var a: integer;", "read(a);\nwriteln(a:1)"), in + 3, 1, ZL_OK, "2147483647\n",
           "maxint as input");
    expect_twin(L,
                "program t(input,output);\nvar a,b:integer;\nbegin\nREAD(a,b);READLN;readln(a)"
                "\nend.",
                "program t(input, output);\nvar\n  a, b: integer;\nbegin\n  read(a, b);\n"
                "  readln;\n  readln(a)\nend.\n");
}

static void test_pascal_twin(void)
{
    const int L = ZL_LANG_PASCAL;
    expect_twin(L, "PROGRAM Hello;BEGIN WRITELN('Hi') END.",
                "program hello;\nbegin\n  writeln('Hi')\nend.\n");
    expect_twin(L, "program p ( Output , INPUT ) ; begin end .",
                "program p(output, input);\nbegin\nend.\n");
    expect_twin(L,
                "program t(output);\nconst A=+5; B=-a; C=TRUE; D=maxint;\nvar x,y:Integer;"
                "f:BOOLEAN;v:array [ -2 .. a ] of Boolean;\nbegin x:=a end.",
                "program t(output);\nconst\n  a = 5;\n  b = -a;\n  c = true;\n  d = maxint;\n"
                "var\n  x, y: integer;\n  f: boolean;\n  v: array[-2..a] of boolean;\nbegin\n"
                "  x := a\nend.\n");
    expect_twin(L, "program t;var x:integer;begin x:=((1+2))*3-(4-5)-(-6)+(+7) end.",
                "program t;\nvar\n  x: integer;\nbegin\n  x := (1 + 2) * 3 - (4 - 5) - (-6) + 7\n"
                "end.\n");
    expect_twin(L,
                "program t;var x:integer;b:boolean;begin x:=-(-x); x:=-x*2; x:=(-x)*2; "
                "x:=-(x*2); x:=2-(-x); b:=-x<(-x); b:=not(not b); b:=not(x>1)or(x<0) end.",
                "program t;\nvar\n  x: integer;\n  b: boolean;\nbegin\n  x := -(-x);\n"
                "  x := -x * 2;\n  x := (-x) * 2;\n  x := -x * 2;\n  x := 2 - (-x);\n"
                "  b := -x < -x;\n  b := not not b;\n  b := not (x > 1) or (x < 0)\nend.\n");
    expect_twin(L,
                "program t;var x:integer;begin x:=1-(2+3); x:=(1-2)+3; x:=1 div (2*3); "
                "x:=(1 div 2)*3; x:=abs(-(1)) end.",
                "program t;\nvar\n  x: integer;\nbegin\n  x := 1 - (2 + 3);\n  x := 1 - 2 + 3;\n"
                "  x := 1 div (2 * 3);\n  x := 1 div 2 * 3;\n  x := abs(-1)\nend.\n");
    expect_twin(L,
                "program t;var i:integer;begin if i=1 then i:=2 else if i=2 then begin i:=3 end "
                "else ; while i<3 do ; for i:=1 to 2 do begin end; repeat until true end.",
                "program t;\nvar\n  i: integer;\nbegin\n  if i = 1 then\n    i := 2\n"
                "  else if i = 2 then\n    begin\n      i := 3\n    end\n  else\n    begin\n"
                "    end;\n  while i < 3 do\n    begin\n    end;\n  for i := 1 to 2 do\n"
                "    begin\n    end;\n  repeat\n  until true\nend.\n");
    expect_twin(L, "program t;var i:integer;begin case i of 1,2:;-3:i:=1;end end.",
                "program t;\nvar\n  i: integer;\nbegin\n  case i of\n    1, 2:\n      begin\n"
                "      end;\n    -3:\n      i := 1\n  end\nend.\n");
    expect_twin(L,
                "program t;var x:boolean;procedure p(a,b:integer;var c:boolean);var d:integer;"
                "function f:integer;begin f:=1 end;begin d:=f end;begin p(1,2+3,x) end.",
                "program t;\nvar\n  x: boolean;\nprocedure p(a, b: integer; var c: boolean);\n"
                "var\n  d: integer;\n  function f: integer;\n  begin\n    f := 1\n  end;\n"
                "begin\n  d := f\nend;\nbegin\n  p(1, 2 + 3, x)\nend.\n");
    expect_twin(L, "program t;begin write('a':3,1:2,true:1);writeln end.",
                "program t;\nbegin\n  write('a':3, 1:2, true:1);\n  writeln\nend.\n");
    /* comments keep their place: before the line they preceded */
    expect_twin(L, "{head}program t;{a}begin{b} writeln(1 {c}) (*d*) end.{e}\n(*f}*)",
                "{head}\nprogram t;\n{a}\nbegin\n  {b}\n  writeln(1)\n{c}\n{d}\nend.\n{e}\n"
                "(*f}*)\n");
    expect_twin(L, "{$MODE ISO}program t;begin end.", "{$MODE ISO}\nprogram t;\nbegin\nend.\n");
    expect_twin(L, "program t;begin if true then{x} else{y} ;{z}end.",
                "program t;\nbegin\n  if true then\n    {x}\n    begin\n    end\n  else\n"
                "    {y}\n    begin\n    end\n{z}\nend.\n");
    expect_twin(L, "program t;var i:integer;begin case i of{a}1{b}:{c}i:=1{d};{e}2:i:=2{f}end end.",
                "program t;\nvar\n  i: integer;\nbegin\n  case i of\n    {a}\n    1:\n      {b}\n"
                "      {c}\n      i := 1;\n    {d}\n    {e}\n    2:\n      i := 2\n  {f}\n  end\n"
                "end.\n");
    expect_twin(L, "{ multi\n   line }\nprogram t;\nbegin\nend.\n",
                "{ multi\n   line }\nprogram t;\nbegin\nend.\n");
    expect_twin(L, "program t;\r\nbegin\r\n\twriteln\r\nend.\r\n",
                "program t;\nbegin\n  writeln\nend.\n");
    {
        zl_diag_t d;
        const char *canon = "program t;\nbegin\n  writeln(1)\nend.\n";
        CHECK(zl_compile(L | ZL_TWIN, canon, strlen(canon), &P, &work, &d) == ZL_OK,
              "canonical Tutordrake is accepted: %s", d.msg);
        CHECK(P.word_bits == 32, "Pascal programs are 32-bit (%u)", P.word_bits);
        CHECK(strcmp(zl_twin_name(L), "Tutordrake") == 0, "the twin is Tutordrake");
        static const char *const off[] = {
            "program t;\nbegin\n  writeln(1)\nend.",
            "program t;\nbegin\nwriteln(1)\nend.\n",
            "PROGRAM t;\nbegin\n  writeln(1)\nend.\n",
            "program t;\nbegin\n  writeln( 1)\nend.\n",
            "program t;\nbegin\n  writeln(1);\nend.\n",
            "program t;\n\nbegin\n  writeln(1)\nend.\n",
            "program t;\nbegin\n  writeln((1))\nend.\n",
            "program t;\nbegin\n  writeln(+1)\nend.\n",
            "program T;\nbegin\n  writeln(1)\nend.\n",
            "program t;\nbegin\n  writeln(1) \nend.\n",
        };
        for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) {
            int s = zl_compile(L | ZL_TWIN, off[i], strlen(off[i]), &P, &work, &d);
            CHECK(s == ZL_E_NONCANON, "non-canonical twin %zu refused (%d)", i, s);
            CHECK(zl_compile(L, off[i], strlen(off[i]), &P, &work, &d) == ZL_OK,
                  "but it is Pascal %zu: %s", i, d.msg);
        }
    }
}

static void test_pascal_refusals(void)
{
    const int L = ZL_LANG_PASCAL;
    const int U = ZL_E_UNSUPPORTED, S = ZL_E_SYNTAX;
    /* outside the subset, refused by name */
    expect_refused(L, PS("writeln(1.5)"), U, 3, "real literal");
    expect_refused(L, PS("writeln(1e3)"), U, 3, "exponent literal");
    expect_refused(L, PS("writeln(2147483648)"), U, 3, "literal past maxint");
    expect_refused(L, PS("writeln(99999999999999999999999)"), U, 3, "very long literal");
    expect_refused(L, PS("writeln(4 / 2)"), U, 3, "real division");
    expect_refused(L, PD("var r: real;", "r := 1"), U, 2, "real variable");
    expect_refused(L, PD("var c: char;", "c := 1"), U, 2, "char variable");
    expect_refused(L, PD("var s: string;", "s := 1"), U, 2, "string variable");
    expect_refused(L, PD("var f: text;", ""), U, 2, "text file");
    expect_refused(L, PS("writeln(chr(65))"), U, 3, "chr");
    expect_refused(L, PS("writeln(trunc(1))"), U, 3, "trunc");
    expect_refused(L, PS("writeln(sqrt(4))"), U, 3, "sqrt");
    expect_refused(L, PS("writeln(round(4))"), U, 3, "round");
    expect_refused(L, PS("if eof then writeln"), U, 3, "eof");
    expect_refused(L, PS("page"), U, 3, "page");
    expect_refused(L, PS("new(p)"), U, 3, "new");
    expect_refused(L, PS("if 'a' = 'a' then writeln"), U, 3, "string comparison");
    expect_refused(L, PD("type t = integer;", ""), U, 2, "type declaration");
    expect_refused(L, PD("var r: record a: integer end;", ""), U, 2, "record");
    expect_refused(L, PD("var s: set of 1..3;", ""), U, 2, "set");
    expect_refused(L, PD("var f: file of integer;", ""), U, 2, "file");
    expect_refused(L, PD("var a: packed array[1..2] of integer;", ""), U, 2, "packed");
    expect_refused(L, PD("var p: ^integer;", ""), U, 2, "pointer type");
    expect_refused(L, PD("var x: 1..10;", ""), U, 2, "subrange type");
    expect_refused(L, PD("const k = 3;\nvar x: k..10;", ""), U, 3, "subrange from a constant");
    expect_refused(L, PD("var c: (red, green);", ""), U, 2, "enumerated type");
    expect_refused(L, PD("var a: array[1..2, 1..2] of integer;", ""), U, 2, "2-D array");
    expect_refused(L, PD("var a: array[1..2] of array[1..2] of integer;", ""), U, 2,
                   "array of arrays");
    expect_refused(L, PD("var a: array[boolean] of integer;", ""), U, 2, "Boolean index type");
    expect_refused(L, PD("var a: array[false..true] of integer;", ""), U, 2, "Boolean bounds");
    expect_refused(L, PD("var a: array[1..2] of integer;", "writeln(a[1, 2])"), U, 4, "2-D index");
    expect_refused(L, PD("var a, b: array[1..2] of integer;", "a := b"), U, 4,
                   "whole-array assignment");
    expect_refused(L, PD("var a: array[1..2] of integer;", "writeln(a)"), U, 4,
                   "whole-array value");
    expect_refused(L, PS("with x do writeln"), U, 3, "with");
    expect_refused(L, PS("goto 10"), U, 3, "goto");
    expect_refused(L, PS("10: writeln"), U, 3, "statement label");
    expect_refused(L, PD("label 10;", ""), U, 2, "label declaration");
    expect_refused(L, PS("writeln(nil)"), U, 3, "nil");
    expect_refused(L, PS("writeln(1 in [1])"), U, 3, "in");
    expect_refused(L, PS("writeln(x^)"), ZL_E_UNDEFINED, 3, "unknown name before ^");
    expect_refused(L, PD("var x: integer;", "writeln(x^)"), S, 4, "pointer dereference");
    expect_refused(L, PD("procedure p; forward;", ""), U, 2, "forward");
    expect_refused(L, PD("procedure p(procedure q);\nbegin end;", ""), U, 2,
                   "procedural parameter");
    expect_refused(L, PD("procedure p(a: array[1..2] of integer);\nbegin end;", ""), U, 2,
                   "array parameter");
    expect_refused(L, PS("writeln(output, 1)"), U, 3, "file argument to write");
    expect_refused(L, PD("var x: integer;", "read(input, x)"), U, 4, "file argument to read");
    expect_refused(L, PD("var x: integer;", "x := 1;\nwriteln(1:x)"), U, 5, "variable width");
    expect_refused(L, PS("writeln(1:1+1)"), U, 3, "expression width");
    expect_refused(L, PS("writeln(1:65)"), U, 3, "width over 64");
    expect_refused(L, PS("writeln(1:5:2)"), U, 3, "real formatting");
    expect_refused(L, PS("case 1 of 1: writeln else writeln end"), S, 3,
                   "else after a case arm without ';'");
    expect_refused(L, PS("case 1 of 1: writeln; else writeln end"), U, 3, "case else");
    expect_refused(L, PS("case true of true: writeln end"), U, 3, "Boolean case selector");
    expect_refused(L, PS("writeln(succ(false))"), U, 3, "succ of a Boolean");
    expect_refused(L, PS("writeln(pred(true))"), U, 3, "pred of a Boolean");
    expect_refused(L, PD("var b: boolean;", "for b := false to true do writeln"), U, 4,
                   "Boolean control variable");
    expect_refused(L, PD("var my_var: integer;", ""), U, 2, "underscore");
    expect_refused(L, PD("var _x: integer;", ""), U, 2, "leading underscore");
    expect_refused(L, "{$R+}\nprogram t;\nbegin\nend.\n", U, 1, "other directive");
    expect_refused(L, "program t;\n(*$I x*)\nbegin\nend.\n", U, 2, "directive in (*$ *)");
    expect_refused(L, "program t;\n{ a { b }\nbegin\nend.\n", U, 2, "nested brace comment");
    expect_refused(L, "program t;\n(* a (* b *)\nbegin\nend.\n", U, 2, "nested paren comment");
    expect_refused(L, "program t(output, f);\nbegin\nend.\n", U, 1, "other program parameter");
    expect_refused(L, PD("const s = 'x';", ""), U, 2, "string constant");
    expect_refused(L, PS("writeln(otherwise)"), ZL_E_UNDEFINED, 3, "unknown name");
    expect_refused(L, PS("frob"), ZL_E_UNDEFINED, 3, "unknown procedure");
    expect_refused(L, PS("x := 1"), ZL_E_UNDEFINED, 3, "unknown variable");
    expect_refused(L, PS("case 1 of otherwise: writeln end"), ZL_E_UNDEFINED, 3,
                   "otherwise is not ISO 7185");
    expect_refused(L, PD("var x: frob;", ""), U, 2, "unknown type name");

    /* not Pascal */
    expect_refused(L, "begin end.", S, 1, "no program heading");
    expect_refused(L, "program;\nbegin\nend.\n", S, 1, "no program name");
    expect_refused(L, "program t\nbegin\nend.\n", S, 2, "no ';' after the heading");
    expect_refused(L, "program t(output output);\nbegin\nend.\n", S, 1, "parameters need ','");
    expect_refused(L, "program t(output, output);\nbegin\nend.\n", S, 1, "parameter twice");
    expect_refused(L, "program t;\nbegin\nend\n", S, 4, "no final '.'");
    expect_refused(L, "program t;\nbegin\nend.\nx", S, 4, "text after the end");
    expect_refused(L, "program t;\nbegin\n", S, 3, "no end");
    expect_refused(L, PS("writeln(1) writeln(2)"), S, 3, "missing ';'");
    expect_refused(L, PD("var x: integer;", "x = 1"), S, 4, "'=' for ':='");
    expect_refused(L, PD("var x: integer;", "x := true"), S, 4, "Boolean into an integer");
    expect_refused(L, PD("var b: boolean;", "b := 1"), S, 4, "integer into a Boolean");
    expect_refused(L, PS("if 1 then writeln"), S, 3, "integer condition");
    expect_refused(L, PS("while 1 do writeln"), S, 3, "integer while");
    expect_refused(L, PS("repeat until 0"), S, 3, "integer until");
    expect_refused(L, PS("writeln(1 and 2)"), S, 3, "and on integers");
    expect_refused(L, PS("writeln(not 1)"), S, 3, "not on an integer");
    expect_refused(L, PS("writeln(true + 1)"), S, 3, "arithmetic on a Boolean");
    expect_refused(L, PS("writeln(-true)"), S, 3, "sign on a Boolean");
    expect_refused(L, PS("writeln(1 = true)"), S, 3, "integer compared with a Boolean");
    expect_refused(L, PS("writeln(abs(true))"), S, 3, "abs of a Boolean");
    expect_refused(L, PS("writeln(odd(true))"), S, 3, "odd of a Boolean");
    expect_refused(L, PS("writeln(abs 3)"), S, 3, "function without '('");
    expect_refused(L, PS("writeln(2 * -3)"), S, 3, "a sign after an operator");
    expect_refused(L, PS("writeln(2 - -3)"), S, 3, "a sign after minus");
    expect_refused(L, PS("writeln(1 < 2 < 3)"), S, 3, "chained comparison");
    expect_refused(L, PS("writeln((1 < 2) = (3 < 4) = true)"), S, 3, "chained equality");
    expect_refused(L, PS("writeln((1 + 2)"), S, 4, "missing ')' (seen at the next token)");
    expect_refused(L, PS("writeln(1 +)"), S, 3, "missing operand");
    expect_refused(L, PS("writeln(begin)"), S, 3, "keyword in an expression");
    expect_refused(L, PS("writeln('abc)"), S, 3, "unterminated string");
    expect_refused(L, PS("writeln(\"abc\")"), S, 3, "double quotes");
    expect_refused(L, PS("writeln(1 # 2)"), S, 3, "stray character");
    expect_refused(L, "program t;\n{ open\nbegin\nend.\n", S, 2, "unterminated comment");
    expect_refused(L, PS("write"), S, 3, "write without items");
    expect_refused(L, PS("writeln()"), S, 3, "empty parentheses");
    expect_refused(L, PS("read"), S, 3, "read without variables");
    expect_refused(L, PD("var b: boolean;", "read(b)"), S, 4, "read of a Boolean");
    expect_refused(L, PS("read(1)"), S, 3, "read of a number");
    expect_refused(L, PS("writeln(1:-1)"), S, 3, "negative width");
    expect_refused(L, PS("writeln(1:true)"), S, 3, "Boolean width");
    expect_refused(L, PS("if true writeln"), S, 3, "missing then");
    expect_refused(L, PS("while true writeln"), S, 3, "missing do");
    expect_refused(L, PD("var i: integer;", "for i = 1 to 2 do writeln"), S, 4, "for without :=");
    expect_refused(L, PD("var i: integer;", "for i := 1 upto 2 do writeln"), S, 4,
                   "for without to");
    expect_refused(L, PD("var i: integer;", "for i := 1 to 2 writeln"), S, 4, "for without do");
    expect_refused(L, PS("repeat writeln"), S, 4, "repeat without until");
    expect_refused(L, PS("case 1 of 1: writeln; 1: writeln end"), S, 3, "duplicate case label");
    expect_refused(L, PD("const k = 1;", "case 1 of 1: writeln; k: writeln end"), S, 4,
                   "duplicate label through a constant");
    expect_refused(L, PS("case 1 of end"), S, 3, "case without arms");
    expect_refused(L, PS("case 1 of 1 writeln end"), S, 3, "case arm without ':'");
    expect_refused(L, PS("case 1 of 1: writeln 2: writeln end"), S, 3, "case arms without ';'");
    expect_refused(L, PS("case 1 of true: writeln end"), S, 3, "Boolean label");
    expect_refused(L, PS("case 1 of 1: writeln"), S, 4, "case without end");
    expect_refused(L, PD("var x, x: integer;", ""), S, 2, "a name twice in one group");
    expect_refused(L, PD("var x: integer; x: boolean;", ""), S, 2, "a name twice in a block");
    expect_refused(L, PD("const k = 1;\nvar k: integer;", ""), S, 3, "constant and variable");
    expect_refused(L, PD("procedure p(a, a: integer);\nbegin end;", ""), S, 2, "a parameter twice");
    expect_refused(L, PD("procedure p;\nbegin end;\nprocedure p;\nbegin end;", ""), S, 4,
                   "a procedure twice");
    expect_refused(L, PD("const k = 1;", "k := 2"), S, 4, "assigning a constant");
    expect_refused(L, PD("procedure p;\nbegin end;", "p := 2"), S, 5, "assigning a procedure");
    expect_refused(L, PD("var i: integer;", "for i := 1 to 2 do i := 5"), S, 4,
                   "assigning the control variable in its loop");
    expect_refused(L, PD("var i: integer;", "for i := 1 to 2 do read(i)"), S, 4,
                   "reading into the control variable");
    expect_refused(
        L,
        PD("var i: integer;\nprocedure p(var x: integer);\nbegin end;", "for i := 1 to 2 do p(i)"),
        S, 6, "passing the control variable as var");
    expect_refused(L, PD("var i: integer;", "for i := 1 to 2 do for i := 1 to 2 do writeln"), S, 4,
                   "nested loops on one variable");
    expect_refused(L,
                   PD("var i: integer;\nprocedure p;\nbegin for i := 1 to 2 do writeln end;", ""),
                   S, 4, "a global control variable in a procedure");
    expect_refused(L, PD("procedure p(i: integer);\nbegin for i := 1 to 2 do writeln end;", ""), S,
                   3, "a parameter as control variable");
    expect_refused(L, PD("var a: array[1..2] of integer;", "for a[1] := 1 to 2 do writeln"), S, 4,
                   "an element as control variable");
    expect_refused(L, PD("function f: integer;\nbegin f := 1 end;", "f"), S, 5,
                   "a function call as a statement");
    expect_refused(L, PD("function f: integer;\nbegin f := 1 end;", "f := 2"), S, 5,
                   "setting a function's result outside it");
    expect_refused(L, PD("procedure p;\nbegin end;", "writeln(p)"), S, 5,
                   "a procedure has no value");
    expect_refused(L, PD("procedure p(a: integer);\nbegin end;", "p"), S, 5, "too few arguments");
    expect_refused(L, PD("procedure p(a: integer);\nbegin end;", "p(1, 2)"), S, 5,
                   "too many arguments");
    expect_refused(L, PD("procedure p(a, b: integer);\nbegin end;", "p(1)"), S, 5,
                   "one argument short");
    expect_refused(L, PD("procedure p;\nbegin end;", "p(1)"), S, 5, "arguments to none");
    expect_refused(L, PD("procedure p(var a: integer);\nbegin end;", "p(1)"), S, 5,
                   "a number for a var parameter");
    expect_refused(L, PD("var x: integer;\nprocedure p(var a: integer);\nbegin end;", "p(x + 1)"),
                   S, 6, "an expression for a var parameter");
    expect_refused(L, PD("procedure p(a: integer);\nbegin end;", "p(true)"), S, 5, "argument type");
    expect_refused(L, PD("var b: boolean;\nprocedure p(var a: integer);\nbegin end;", "p(b)"), S, 6,
                   "var argument type");
    expect_refused(L, PD("function f(a: integer);\nbegin end;", ""), S, 2,
                   "a function without a result type");
    expect_refused(L, PD("procedure p: integer;\nbegin end;", ""), S, 2,
                   "a procedure with a result type");
    expect_refused(L, PD("var x: integer;\nconst k = 1;", ""), S, 3, "const after var");
    expect_refused(L, PD("procedure p;\nbegin end;\nvar x: integer;", ""), S, 4,
                   "var after procedures");
    expect_refused(L, PD("var a: array[3..1] of integer;", ""), S, 2, "empty bounds");
    expect_refused(L, PD("var x: integer;", "x[1] := 2"), S, 4, "index on a scalar");
    expect_refused(L, PD("var x: integer;", "x := x[1]"), S, 4,
                   "index on a scalar in an expression");
    expect_refused(L, PD("var a: array[1..2] of integer;", "a[true] := 2"), S, 4, "Boolean index");
    expect_refused(L, PD("const k = -true;", ""), S, 2, "sign on a Boolean constant");
    expect_refused(L, PD("var x: integer;\nconst k = x;", ""), S, 3, "not a constant");
    expect_refused(L, PD("var x: integer;", "x := 1;\nwriteln(1:+x)"), U, 5,
                   "a signed variable width");
    expect_out(L, PS("if false then writeln else"), "", "else before end is an empty branch");
    expect_refused(L, PS("procedure"), S, 3, "procedure in a statement");

    /* capacities */
    {
        static char big[ZL_MAX_LINE + 128];
        memset(big, ' ', sizeof big);
        memcpy(big, "program t;", 10);
        memcpy(big + sizeof big - 12, "\nbegin end.", 11);
        big[sizeof big - 1] = 0;
        expect_refused(L, big, ZL_E_LIMIT, 1, "line too long");
        memset(big, ' ', sizeof big);
        memcpy(big, "program t;\nbegin end.", 21);
        big[sizeof big - 1] = 0;
        expect_refused(L, big, ZL_E_LIMIT, 2, "last line too long");
    }
    {
        static char src[4096];
        size_t n = (size_t) snprintf(src, sizeof src, "program t;\nbegin\nwriteln(");
        for (int k = 0; k < ZL_MAX_DEPTH + 2; k++) src[n++] = '(';
        src[n++] = '1';
        for (int k = 0; k < ZL_MAX_DEPTH + 2; k++) src[n++] = ')';
        snprintf(src + n, sizeof src - n, ")\nend.\n");
        expect_refused(L, src, ZL_E_LIMIT, 3, "parentheses nested too deeply");
        n = (size_t) snprintf(src, sizeof src, "program t;\nbegin\nwriteln(");
        for (int k = 0; k < 30; k++) src[n++] = '(';
        src[n++] = '1';
        for (int k = 0; k < 30; k++) src[n++] = ')';
        snprintf(src + n, sizeof src - n, ":1)\nend.\n");
        expect_out(L, src, "1\n", "30 nested parentheses are fine");
        n = (size_t) snprintf(src, sizeof src, "program t;\nbegin\nwriteln(1");
        for (int k = 0; k < ZL_MAX_DEPTH + 2; k++) n += (size_t) snprintf(src + n, 8, " + 1");
        snprintf(src + n, sizeof src - n, ")\nend.\n");
        expect_refused(L, src, ZL_E_LIMIT, 3, "an expression too deep to print");
        n = (size_t) snprintf(src, sizeof src, "program t;\nbegin\n");
        for (int k = 0; k < ZL_MAX_DEPTH + 2; k++) n += (size_t) snprintf(src + n, 8, "begin ");
        for (int k = 0; k < ZL_MAX_DEPTH + 2; k++) n += (size_t) snprintf(src + n, 8, "end ");
        snprintf(src + n, sizeof src - n, "\nend.\n");
        expect_refused(L, src, ZL_E_LIMIT, 3, "statements nested too deeply");
        n = (size_t) snprintf(src, sizeof src, "program t;\n");
        for (int k = 0; k < 16; k++) n += (size_t) snprintf(src + n, 32, "procedure p%d;\n", k);
        n += (size_t) snprintf(src + n, 32, "begin end;\n");
        for (int k = 0; k < 15; k++) n += (size_t) snprintf(src + n, 32, "begin end;\n");
        snprintf(src + n, sizeof src - n, "begin\nend.\n");
        expect_refused(L, src, ZL_E_LIMIT, 17, "procedures nested 16 deep");
        n = (size_t) snprintf(src, sizeof src, "program t;\n");
        for (int k = 0; k < 15; k++) n += (size_t) snprintf(src + n, 32, "procedure p%d;\n", k);
        n += (size_t) snprintf(src + n, 32, "begin writeln('deep') end;\n");
        for (int k = 13; k >= 0; k--)
            n += (size_t) snprintf(src + n, 32, "begin p%d end;\n", k + 1);
        snprintf(src + n, sizeof src - n, "begin\np0\nend.\n");
        expect_out(L, src, "deep\n", "procedures nested 15 deep");
    }
    expect_refused(L, PD("var a: array[1..5000] of integer;", ""), ZL_E_LIMIT, 2,
                   "a global array past the slots");
    expect_refused(L, PD("procedure p;\nvar a: array[1..5000] of integer;\nbegin end;", ""),
                   ZL_E_LIMIT, 3, "a local array past the frame");
    expect_refused(L, PD("procedure p;\nvar a, b: array[1..3000] of integer;\nbegin end;", ""),
                   ZL_E_LIMIT, 3, "a frame too large");
}

static void test_pascal_traps(void)
{
    const int L = ZL_LANG_PASCAL;
    expect_trap(L, PS("while true do"), ZL_E_FUEL, 0, "endless loop");
    expect_trap(L, PD("var x: integer;", "x := maxint;\nx := x + 1"), ZL_E_OVERFLOW, 5,
                "overflow on its own line");
    expect_trap(L, PD("var a: array[1..2] of integer;", "a[1] := 1;\n\nwriteln(a[a[1] + 2])"),
                ZL_E_BOUNDS, 6, "bounds on its own line");
    expect_trap(L, PS("writeln(1);\nwriteln(maxint * 2)"), ZL_E_OVERFLOW, 4,
                "the trap line, after output");
    CHECK(strcmp(out, "          1\n") == 0, "output before a trap is kept: [%s]", out);
    {
        int64_t tr[8];
        zl_trace_t t = {tr, 8, 0};
        zl_diag_t d;
        const char *src = PS("writeln(3, -4:2, true);\nwrite(5:9)");
        CHECK(compile(L, src, &d) == ZL_OK, "trace program compiles: %s", d.msg);
        memset(&R, 0, sizeof R);
        R.out = out;
        R.out_cap = sizeof out;
        R.trace = &t;
        CHECK(zl_run(&P, &R) == ZL_OK && t.n == 3 && tr[0] == 3 && tr[1] == -4 && tr[2] == 5,
              "the trace has the integers printed, not the Booleans (%zu)", t.n);
    }
}

/* The IR of a program as "OP a b; ..." text. */
static const char *pas_ir(const char *src)
{
    static char buf[1 << 14];
    zl_diag_t d;
    size_t n = 0;
    buf[0] = 0;
    if (compile(ZL_LANG_PASCAL, src, &d) != ZL_OK) {
        snprintf(buf, sizeof buf, "refused: %s", d.msg);
        return buf;
    }
    for (uint32_t i = 0; i < P.ncode && n + 64 < sizeof buf; i++)
        n += (size_t) snprintf(buf + n, sizeof buf - n, "%s%s %lld %d", i ? "; " : "",
                               zl_op_name(P.code[i].op), (long long) P.code[i].a, P.code[i].b);
    return buf;
}

static void expect_ir(const char *src, const char *want, const char *what)
{
    const char *got = pas_ir(src);
    CHECK(strcmp(got, want) == 0, "%s IR:\n got  %s\n want %s", what, got, want);
}

static void test_pascal_ir(void)
{
    expect_ir("program t;\nvar x: integer;\nbegin\nx := 7 mod x\nend.\n",
              "PUSH 7 0; LOAD 0 0; DUP 0 0; PUSH 0 0; LT 0 0; JZ 7 0; TRAP -2629 0; MODF 0 0; "
              "STORE 0 0; HALT 0 0",
              "ISO mod checks its divisor");
    expect_ir(
        "program t;\nvar x: integer;\nbegin\nx := -x div 2 + abs(x) - sqr(x)\nend.\n",
        "LOAD 0 0; PUSH 2 0; DIVT 0 0; NEG 0 0; LOAD 0 0; ABS 0 0; ADD 0 0; LOAD 0 0; DUP 0 0; "
        "MUL 0 0; SUB 0 0; STORE 0 0; HALT 0 0",
        "sign, div, abs, sqr");
    expect_ir(
        "program t;\nvar b: boolean; x: integer;\nbegin\nb := odd(x) and (x < 3) or not b;\n"
        "x := succ(pred(ord(b)))\nend.\n",
        "LOAD 1 0; PUSH 2 0; MODF 0 0; DUP 0 0; JZ 9 0; DROP 0 0; LOAD 1 0; PUSH 3 0; LT 0 0; "
        "DUP 0 0; JNZ 14 0; DROP 0 0; LOAD 0 0; NOT 0 0; STORE 0 0; LOAD 0 0; PUSH 1 0; "
        "SUB 0 0; PUSH 1 0; ADD 0 0; STORE 1 0; HALT 0 0",
        "short-circuit and/or, odd, succ, pred, ord");
    expect_ir("program t;\nvar i: integer;\nbegin\nfor i := 1 to 3 do write(i)\nend.\n",
              "PUSH 1 0; PUSH 3 0; STORE 1 0; STORE 0 0; LOAD 0 0; LOAD 1 0; GT 0 0; JNZ 19 0; "
              "LOAD 0 0; OUTI 0 11; LOAD 0 0; LOAD 1 0; EQ 0 0; JNZ 19 0; LOAD 0 0; PUSH 1 0; "
              "ADD 0 0; STORE 0 0; JMP 8 0; HALT 0 0",
              "for to");
    expect_ir("program t;\nvar i: integer;\nbegin\nfor i := 3 downto 1 do\nend.\n",
              "PUSH 3 0; PUSH 1 0; STORE 1 0; STORE 0 0; LOAD 0 0; LOAD 1 0; LT 0 0; JNZ 17 0; "
              "LOAD 0 0; LOAD 1 0; EQ 0 0; JNZ 17 0; LOAD 0 0; PUSH 1 0; SUB 0 0; STORE 0 0; "
              "JMP 8 0; HALT 0 0",
              "for downto");
    expect_ir(
        "program t;\nvar b: boolean;\nbegin\nwhile b do b := false;\nrepeat until b;\n"
        "if b then writeln else write(1:3)\nend.\n",
        "LOAD 0 0; JZ 5 0; PUSH 0 0; STORE 0 0; JMP 0 0; LOAD 0 0; JZ 5 0; LOAD 0 0; JZ 11 0; "
        "NL 0 0; JMP 13 0; PUSH 1 0; OUTI 0 3; HALT 0 0",
        "while, repeat, if/else");
    expect_ir(
        "program t;\nbegin\ncase 2 of 1, 2: writeln; -4: end\nend.\n",
        "PUSH 2 0; JMP 7 0; DROP 0 0; NL 0 0; JMP 20 0; DROP 0 0; JMP 20 0; DUP 0 0; PUSH 1 0; "
        "EQ 0 0; JNZ 2 0; DUP 0 0; PUSH 2 0; EQ 0 0; JNZ 2 0; DUP 0 0; PUSH -4 0; EQ 0 0; "
        "JNZ 5 0; TRAP -2629 0; HALT 0 0",
        "case dispatch");
    expect_ir("program t;\nbegin\nwrite(true:2, 'ab':3)\nend.\n",
              "PUSH 1 0; JZ 4 0; OUTS 0 0; JMP 5 0; OUTS 1 0; OUTS 2 0; HALT 0 0",
              "Boolean and string widths");
    expect_ir(
        "program t;\nvar g: integer; v: array[2..4] of integer;\n"
        "procedure p(a: integer; var r: integer);\nvar l: integer; w: array[-1..1] of integer;\n"
        "begin l := a; r := l; w[0] := r; v[3] := w[a] end;\nbegin\np(g, v[4])\nend.\n",
        "JMP 30 0; ENTER 6 2; LOADF 0 0; STOREF 2 0; LOADF 2 0; LOADF 1 0; STOREA 0 0; "
        "PUSH 0 0; PUSH -1 0; SUB 0 0; BOUND 3 0; ADDRF 3 0; ADD 0 0; LOADF 1 0; LOADA 0 0; "
        "SWAP 0 0; STOREA 0 0; PUSH 3 0; PUSH 2 0; SUB 0 0; LOADF 0 0; PUSH -1 0; SUB 0 0; "
        "BOUND 3 0; ADDRF 3 0; ADD 0 0; LOADA 0 0; STOREX 1 3; LEAVE 0 0; RET 0 0; "
        "LINK 0 0; LOAD 0 0; PUSH 4 0; PUSH 2 0; "
        "SUB 0 0; BOUND 3 0; PUSH 1 0; ADD 0 0; CALL 1 0; HALT 0 0",
        "frames, var parameters, local and global arrays");
    expect_ir("program t;\nfunction f(n: integer): integer;\n  procedure s;\n  begin f := n end;\n"
              "begin s end;\nbegin\nwriteln(f(1))\nend.\n",
              "JMP 13 0; ENTER 2 1; JMP 8 0; ENTER 0 0; LOADF 0 1; STOREF 1 1; LEAVE 0 0; RET 0 0; "
              "LINK 0 0; CALL 3 0; LOADF 1 0; LEAVE 0 0; RET 0 0; LINK 0 0; "
              "PUSH 1 0; CALL 1 0; OUTI 0 11; NL 0 0; HALT 0 0",
              "a function's result set from a nested procedure");
    {
        zl_diag_t d;
        CHECK(compile(ZL_LANG_PASCAL, "program t;\nbegin\n  x := 1\nend.\n", &d) ==
                      ZL_E_UNDEFINED &&
                  d.line == 3 && d.col == 3,
              "unknown names point at their column (%u:%u)", d.line, d.col);
        CHECK(compile(ZL_LANG_PASCAL, "program t;\nbegin\n  writeln(1.5)\nend.\n", &d) ==
                      ZL_E_UNSUPPORTED &&
                  d.line == 3 && d.col == 11,
              "a real literal is refused at its column (%u:%u)", d.line, d.col);
        CHECK(compile(ZL_LANG_PASCAL, "program t;\n{ x\n\n  y }  @\nbegin\nend.\n", &d) ==
                      ZL_E_SYNTAX &&
                  d.line == 4 && d.col == 8,
              "lines are counted inside comments (%u:%u)", d.line, d.col);
        CHECK(compile(ZL_LANG_PASCAL, "program t;\nbegin\nend.\n", &d) == ZL_OK && P.nslots == 0 &&
                  P.ncode == 1 && P.lang == ZL_LANG_PASCAL,
              "the empty program is one HALT");
    }
}

static void test_pascal_more(void)
{
    const int L = ZL_LANG_PASCAL;
    expect_out(L, "{$Mode Iso}\n(*$mode iso*)\nprogram t(output);\nbegin\nwriteln(1:1)\nend.\n",
               "1\n", "the directive in any case and in (*$ *)");
    expect_twin(L, "program t;(* a } b *)begin end.", "program t;\n(* a } b *)\nbegin\nend.\n");
    expect_twin(L, "program t;(**)begin end.", "program t;\n{}\nbegin\nend.\n");
    expect_out(L, PS("writeln('abc':3, 'abc':4, true:4, false:5, false:6)"),
               "abc abctruefalse false\n", "widths equal to the length");
    expect_out(L,
               PD("var i, j: integer;",
                  "for i := 1 to 3 do\ncase i of\n1: case i + 1 of 2: write('a'); 3: write('b') "
                  "end;\n2, 3: write(i:2)\nend"),
               "a 2 3", "case inside case");
    expect_out(L,
               PD("var n: integer;\nprocedure count(k: integer; var acc: integer);\n"
                  "begin if k > 0 then begin acc := acc + k; count(k - 1, acc) end end;",
                  "n := 0;\ncount(10, n);\nwriteln(n:1)"),
               "55\n", "recursion through a var parameter");
    expect_out(L,
               PD("var r: integer;\nfunction a(n: integer): integer;\n"
                  "  function b(m: integer): integer;\n"
                  "    function c(k: integer): integer;\n    begin c := k + n + m end;\n"
                  "  begin b := c(m * 10) end;\nbegin a := b(n * 100) end;",
                  "r := a(1);\nwriteln(r:1)"),
               "1101\n", "three levels of static links");
    expect_out(
        L,
        PD("var x: integer;\nprocedure p(var a, b: integer);\nbegin a := a + 1; b := b * 10 end;",
           "x := 1;\np(x, x);\nwriteln(x:1)"),
        "20\n", "two var parameters on one variable");
    expect_out(L,
               PD("var v: array[0..1] of boolean; i: integer;",
                  "for i := 0 to 1 do v[i] := i = 1;\nwriteln(v[0], v[1], v[0] < v[1])"),
               "false true true\n", "Boolean arrays");
    expect_out(L, PS("writeln(1 + 2 * 3 - 4 div 2 mod 3:1, ' ', (1 - 2) * (3 - 4):1)"), "5 1\n",
               "mixed precedence");
    expect_out(L, PS("writeln(not (true and false) = true, (1 < 2) <> (3 < 2))"), " true true\n",
               "comparisons of Booleans");
    expect_out(L, PD("const a = 5; b = a; c = -b; d = false;", "writeln(a + b + c:1, d)"),
               "5false\n", "constants built from constants");
    expect_out(L,
               PD("var x: integer;", "x := 5;\nif x > 3 then if x > 4 then write('a') else "
                                     "write('b') else write('c')"),
               "a", "nested if with both elses");
    expect_trap(
        L,
        PD("var a: array[1..2] of integer;\nprocedure p(var x: integer);\nbegin end;", "p(a[0])"),
        ZL_E_BOUNDS, 6, "var argument below its array");
    {
        int64_t in[1] = {5};
        pas_in(PD("var a: array[1..3] of integer;", "read(a[3]);\nwriteln(a[3] * 2:1)"), in, 1,
               ZL_OK, "10\n", "read into a global element");
        pas_in(PD("var a: array[1..3] of integer;", "read(a[4])"), in, 1, ZL_E_BOUNDS, "",
               "read past an array");
        pas_in(
            PD("procedure p;\nvar a: array[1..3] of integer;\nbegin read(a[2]); write(a[2]:1) end;",
               "p"),
            in, 1, ZL_OK, "5", "read into a local element");
    }
    expect_refused(L, PD("function f: integer;\nbegin f := true end;", ""), ZL_E_SYNTAX, 3,
                   "wrong result type");
    expect_refused(L, PD("function f: boolean;\nbegin end;", "writeln(f + 1)"), ZL_E_SYNTAX, 5,
                   "Boolean function in arithmetic");
    expect_refused(L, PD("var x: integer;", "x := (1"), ZL_E_SYNTAX, 5, "unclosed parenthesis");
    expect_refused(L, PD("var x: integer;", "x := f(1)"), ZL_E_UNDEFINED, 4, "unknown function");
    expect_refused(L, PD("var a: array[1..2] of integer;", "a[1 := 2"), ZL_E_SYNTAX, 4,
                   "unclosed index");
    expect_refused(L, "program t;\nvar x: integer\nbegin\nend.\n", ZL_E_SYNTAX, 3,
                   "var without ';'");
    expect_refused(L, "program t;\nconst k 1;\nbegin\nend.\n", ZL_E_SYNTAX, 2, "const without '='");
    expect_refused(L, "program t;\nconst k = ;\nbegin\nend.\n", ZL_E_SYNTAX, 2,
                   "const without a value");
    expect_refused(L, "program t;\nvar a: array[1 2] of integer;\nbegin\nend.\n", ZL_E_SYNTAX, 2,
                   "bounds without '..'");
    expect_refused(L, "program t;\nvar a: array[1..2] integer;\nbegin\nend.\n", ZL_E_SYNTAX, 2,
                   "array without of");
    expect_refused(L, "program t;\nprocedure p(var: integer);\nbegin end;\nbegin\nend.\n",
                   ZL_E_SYNTAX, 2, "parameter without a name");
    expect_refused(L, "program t;\nprocedure p;\nbegin end\nbegin\nend.\n", ZL_E_SYNTAX, 4,
                   "procedure without ';'");
    expect_refused(L, "program t;\nbegin\nend;\n", ZL_E_SYNTAX, 3, "';' for the final '.'");
}

static void test_pascal(void)
{
    test_pascal_ir();
    test_pascal_more();
    test_pascal_output();
    test_pascal_arith();
    test_pascal_statements();
    test_pascal_procs();
    test_pascal_read();
    test_pascal_twin();
    test_pascal_refusals();
    test_pascal_traps();
}
