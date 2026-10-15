/* test_zl_delphi.inc — Delphi (Formwyrm) front-end tests, included by test_zl.c.
 * Expected outputs are what fpc 3.2 -Mdelphi -Co -Cr prints for the same
 * program; each trap is an error there too (a run-time error), except
 * the ones marked [fpc differs], where fpc wraps or computes in 64 bits. */

/* program t; [decls] begin <body> end.  DS's body starts on line 3; DD's
 * declaration is line 2 and its body line 4. */
#define DS(body)       "program t;\nbegin\n" body "\nend.\n"
#define DD(decl, body) "program t;\n" decl "\nbegin\n" body "\nend.\n"

static void test_delphi_output(void)
{
    const int L = ZL_LANG_DELPHI;
    expect_out(L, DS("WriteLn(1, -2, 345)"), "1-2345\n", "integers take as many columns as they need");
    expect_out(L, DS("Write(5:4, -5:4, 123:1, 7:0, 12345:3)"), "   5  -5123712345",
               "integer widths pad but never cut");
    expect_out(L, DS("WriteLn(True, False, ' ', True:6, False:2, '|')"), "TRUEFALSE   TRUEFALSE|\n",
               "Booleans are TRUE and FALSE, padded but not cut");
    expect_out(L, DS("WriteLn('it''s', 'a':3, 'abc':2, '':2, '|')"), "it's  aabc  |\n",
               "strings pad on the left and are never cut");
    expect_out(L, DS("WriteLn;\nWrite('a'); Write('b'); WriteLn"), "\nab\n", "Write and WriteLn");
    expect_out(L, DD("const w = 4;", "WriteLn(7:w, 'x':w)"), "   7   x\n", "constant widths");
    expect_out(L, DS("WriteLn(1 = 1, 1 < 1)"), "TRUEFALSE\n", "Boolean results print");
    expect_out(L, DS("writeln(MaxInt, ' ', -MaxInt - 1)"), "2147483647 -2147483648\n",
               "MaxInt and the most negative Integer");
    expect_out(L, DS("WriteLn($FF, ' ', -$10, ' ', $7FFFFFFF)"), "255 -16 2147483647\n",
               "hexadecimal literals");
}

static void test_delphi_arith(void)
{
    const int L = ZL_LANG_DELPHI;
    expect_out(L,
               DS("WriteLn(2 + 3 * 4, ' ', (2 + 3) * 4, ' ', 10 - 4 - 3, ' ', 100 div 10 div 2, "
                  "' ', 2 * 3 mod 4)"),
               "14 20 3 5 2\n", "precedence and left association");
    expect_out(L, DS("WriteLn(7 div 2, ' ', -7 div 2, ' ', 7 div -2, ' ', -7 div -2, ' ', 0 div 5)"),
               "3 -3 -3 3 0\n", "div truncates toward zero");
    expect_out(L,
               DS("WriteLn(7 mod 3, ' ', -7 mod 3, ' ', 7 mod -3, ' ', -7 mod -3, ' ', -6 mod 3)"),
               "1 -1 1 -1 0\n", "mod takes the sign of the dividend");
    expect_out(L, DS("WriteLn(2 * -3, ' ', - -3, ' ', -2 * 3, ' ', 4 - -1, ' ', +5)"),
               "-6 3 -6 5 5\n", "a sign may start any factor");
    expect_out(L, DS("WriteLn(Abs(-4), ' ', Sqr(-3), ' ', Succ(4), ' ', Pred(4), ' ', Ord(True))"),
               "4 9 5 3 1\n", "Abs Sqr Succ Pred Ord");
    expect_out(L, DS("WriteLn(Odd(3), Odd(-3), Odd(0), Odd(-4))"), "TRUETRUEFALSEFALSE\n", "Odd");
    expect_out(L, DS("WriteLn(5 and 3, ' ', 5 or 3, ' ', 5 xor 3, ' ', not 5, ' ', -1 and 255)"),
               "1 7 6 -6 255\n", "and, or, xor and not on integers are bitwise");
    expect_out(L,
               DS("WriteLn(True xor False, True xor True, not True, True and False, "
                  "False or True)"),
               "TRUEFALSEFALSEFALSETRUE\n", "Boolean xor, not, and, or");
    expect_out(L,
               DD("var z: Integer;", "z := 0;\nWriteLn((z > 0) and (1 div z > 0), "
                                     "(z = 0) or (1 mod z = 0))"),
               "FALSETRUE\n", "and and or stop early");
    expect_out(L, DS("WriteLn(1 < 2, 2 <= 2, 3 > 4, 4 >= 5, 5 = 5, 6 <> 6, False < True)"),
               "TRUETRUEFALSEFALSETRUEFALSETRUE\n", "comparisons");
    expect_out(L, DD("var x: LongInt;", "x := 46340;\nWriteLn(Sqr(x))"), "2147395600\n",
               "LongInt is Integer");
    expect_trap(L, DD("var x: Integer;", "x := MaxInt;\nx := x + 1"), ZL_E_OVERFLOW, 5,
                "an Integer past MaxInt");
    expect_trap(L, DD("var x: Integer;", "x := -MaxInt;\nx := x - 2"), ZL_E_OVERFLOW, 5,
                "an Integer below the least");
    expect_trap(L, DD("var x: Integer;", "x := 65536;\nx := x * 32768"), ZL_E_OVERFLOW, 5,
                "a product past 32 bits");
    /* fpc checks neither Sqr nor Abs for overflow (it wraps them); any
     * result outside 32 bits traps here */
    expect_trap(L, DD("var x: Integer;", "x := 46341;\nx := Sqr(x)"), ZL_E_OVERFLOW, 5,
                "[fpc differs] Sqr past 32 bits");
    expect_trap(L, DD("var x: Integer;", "x := -MaxInt - 1;\nx := -x"), ZL_E_OVERFLOW, 5,
                "negating the least Integer");
    expect_trap(L, DD("var x: Integer;", "x := -MaxInt - 1;\nx := Abs(x)"), ZL_E_OVERFLOW, 5,
                "[fpc differs] Abs of the least Integer");
    expect_trap(L, DD("var x: Integer;", "x := MaxInt;\nInc(x)"), ZL_E_OVERFLOW, 5,
                "Inc past MaxInt");
    expect_trap(L, DD("var x: Integer;", "x := -MaxInt - 1;\nDec(x, 1)"), ZL_E_OVERFLOW, 5,
                "Dec below the least");
    /* fpc evaluates this in 64 bits and prints 2147483648; Delphi with
     * {$Q+} raises EIntOverflow, and so does Lingua */
    expect_trap(L, DS("WriteLn(MaxInt + 1)"), ZL_E_OVERFLOW, 3, "[fpc differs] MaxInt + 1");
    expect_trap(L, DD("var z: Integer;", "z := 0;\nWriteLn(1 div z)"), ZL_E_DIVZERO, 5,
                "div by zero");
    expect_trap(L, DD("var z: Integer;", "z := 0;\nWriteLn(1 mod z)"), ZL_E_DIVZERO, 5,
                "mod by zero");
}

static void test_delphi_statements(void)
{
    const int L = ZL_LANG_DELPHI;
    expect_out(L,
               DD("var i: Integer;",
                  "i := 3;\nif i > 2 then WriteLn('big') else WriteLn('small');\n"
                  "if i = 1 then WriteLn('one') else if i = 3 then WriteLn('three')"),
               "big\nthree\n", "if then else, else if");
    expect_out(L,
               DD("var i, s: Integer;",
                  "i := 0; s := 0;\nwhile i < 10 do begin i := i + 1; s := s + i end;\nWriteLn(s)"),
               "55\n", "while");
    expect_out(L, DD("var i: Integer;", "i := 0;\nrepeat i := i + 2; Write(i:2) until i >= 6"),
               " 2 4 6", "repeat");
    expect_out(L,
               DD("var i: Integer;", "for i := 1 to 4 do Write(i);\nfor i := 4 downto 1 do Write(i);"
                                     "\nfor i := 2 to 1 do Write('x')"),
               "12344321", "for to and downto");
    expect_out(L, DD("var i: Integer;", "for i := MaxInt - 1 to MaxInt do Write(i - MaxInt)"),
               "-10", "for up to MaxInt does not overflow");
    expect_out(L,
               DD("var i: Integer;",
                  "for i := 0 to 7 do\ncase i of\n1..3, 5: Write('a');\n0: Write('z');\n"
                  "6: Write('s')\nelse\nWrite('e'); Write('.')\nend"),
               "zaaae.ase.", "case with ranges and an else list");
    expect_out(L, DD("var i: Integer;", "i := 9;\ncase i of 1: Write('a'); 2: Write('b') end;\n"
                                        "WriteLn('|')"),
               "|\n", "a case with no matching label and no else does nothing");
    expect_out(L, DD("var i: Integer;", "i := 2;\ncase i of 1: Write('a') else Write('e') end"),
               "e", "else without a ';' before it");
    expect_out(L,
               DD("var i: Integer;",
                  "for i := 1 to 10 do begin\nif i = 3 then Continue;\nif i = 6 then Break;\n"
                  "Write(i)\nend"),
               "1245", "Break and Continue in for");
    expect_out(L,
               DD("var i: Integer;",
                  "i := 0;\nwhile True do begin\nInc(i);\nif i > 5 then Break;\n"
                  "if Odd(i) then Continue;\nWrite(i)\nend"),
               "24", "Break and Continue in while");
    expect_out(L,
               DD("var i: Integer;",
                  "i := 0;\nrepeat\nInc(i);\nif i < 3 then Continue;\nWrite(i)\nuntil i >= 4"),
               "34", "Continue in repeat tests the condition");
    expect_out(L,
               DD("var i, j: Integer;",
                  "for i := 1 to 3 do begin\nfor j := 1 to 3 do begin\nif j = 2 then Break;\n"
                  "Write(i, j)\nend;\nif i = 2 then Continue;\nWrite('.')\nend"),
               "11.2131.", "Break leaves the innermost loop");
    expect_out(L,
               DD("var i: Integer; a: array[-1..1] of Integer;",
                  "i := 5; Inc(i); Inc(i, 10); Dec(i); Dec(i, 3);\na[0] := 1; Inc(a[0], 4);\n"
                  "WriteLn(i, ' ', a[0])"),
               "12 5\n", "Inc and Dec");
    expect_out(L, DS("begin end;\n;;"), "", "empty statements");
    expect_out(L, DS("WriteLn('a');\nExit;\nWriteLn('b')"), "a\n", "Exit ends the program");
    expect_out(L, DD("var Abc: Integer;", "aBC := 4;\nWriteLn(ABC)"), "4\n",
               "names in any case are one name");
    expect_out(L, DD("var my_var, _x: Integer;", "my_var := 1; _x := 2;\nWriteLn(my_var + _x)"),
               "3\n", "underscores in names");
}

static void test_delphi_procs(void)
{
    const int L = ZL_LANG_DELPHI;
    expect_out(L,
               DD("function F(n: Integer): Integer;\nbegin\nResult := 1;\nif n < 2 then Exit;\n"
                  "Result := n * F(n - 1)\nend;",
                  "WriteLn(F(10))"),
               "3628800\n", "Result and Exit");
    expect_out(L,
               DD("function G(n: Integer): Integer;\nbegin\nif n > 3 then Exit(n * 2);\nG := n\nend;",
                  "WriteLn(G(5), ' ', G(2))"),
               "10 2\n", "Exit(value) and the function's name");
    expect_out(L,
               DD("function F(n: Integer): Integer;\n  procedure Bump;\n  begin\n"
                  "    Result := Result + 10\n  end;\nbegin\nResult := n;\nBump\nend;",
                  "WriteLn(F(1))"),
               "11\n", "Result from a nested procedure");
    expect_out(L,
               DD("var a, b: Integer;\nprocedure Swap(var x, y: Integer);\nvar t: Integer;\n"
                  "begin t := x; x := y; y := t end;",
                  "a := 1; b := 2;\nSwap(a, b);\nWriteLn(a, b)"),
               "21\n", "var parameters");
    expect_out(L,
               DD("procedure P();\nbegin Write('p') end;\nfunction H: Integer;\nbegin H := 4 end;",
                  "P(); P;\nWriteLn(H(), H)"),
               "pp44\n", "calls with and without empty parentheses");
    expect_out(L,
               DD("procedure P(n: Integer);\nbegin\nif n > 2 then Exit;\nWrite(n);\nP(n + 1)\nend;",
                  "P(0)"),
               "012", "Exit in a procedure");
    expect_out(L,
               DD("var g: Integer;\nprocedure A;\nbegin g := g + 1 end;\nconst k = 5;\n"
                  "var h: Integer;\nfunction B: Integer;\nbegin B := k + g end;",
                  "A; h := B;\nWriteLn(h)"),
               "6\n", "declarations in any order");
    expect_out(L,
               DD("var v: array[1..3] of Integer;\nprocedure Dbl(var x: Integer);\n"
                  "begin x := x * 10 end;",
                  "v[2] := 4;\nDbl(v[2]);\nWriteLn(v[2])"),
               "40\n", "an array element as a var argument");
    expect_out(L,
               DD("procedure P(var x: Integer);\nbegin Inc(x, 2) end;\nvar a: Integer;",
                  "a := 1;\nP(a);\nWriteLn(a)"),
               "3\n", "Inc through a var parameter");
    expect_out(L,
               DD("function Fib(n: Integer): Integer;\nbegin\nif n < 2 then Result := n\n"
                  "else Result := Fib(n - 1) + Fib(n - 2)\nend;",
                  "WriteLn(Fib(20))"),
               "6765\n", "recursion");
}

static void test_delphi_read(void)
{
    int64_t in[3] = {6, -7, 8};
    int s = run_src(ZL_LANG_DELPHI, DD("var a, b: Integer;", "Read(a, b);\nWriteLn(a * b)"), in, 2, 0);
    CHECK(s == ZL_OK && strcmp(out, "-42\n") == 0, "Delphi Read takes the input list (%d [%s])", s,
          out);
    s = run_src(ZL_LANG_DELPHI, DD("var a: Integer;", "ReadLn(a);\nReadLn(a)"), in, 1, 0);
    CHECK(s == ZL_E_INPUT, "Delphi ReadLn past the input traps (%d)", s);
}

static void test_delphi_twin(void)
{
    const int L = ZL_LANG_DELPHI;
    expect_twin(L, "PROGRAM Hello;BEGIN WRITELN('Hi') END.",
                "program hello;\nbegin\n  writeln('Hi')\nend.\n");
    expect_twin(L,
                "program t;{$APPTYPE CONSOLE}\nvar X:LongInt; // count\nfunction F:Integer;begin "
                "Result:=X;Exit(2) end;begin x:=-2*-F() xor $F;Inc(x);Dec(X,3) end.",
                "program t;\n{$APPTYPE CONSOLE}\nvar\n  x: integer;\n// count\nfunction f: integer;\n"
                "begin\n  result := x;\n  exit(2)\nend;\nbegin\n  x := (-2) * (-f) xor 15;\n"
                "  inc(x);\n  dec(x, 3)\nend.\n");
    expect_twin(L,
                "program t;var i:integer;begin case i of 1..2,5:;else i:=1;i:=2 end;case i of "
                "1:i:=0 else end end.",
                "program t;\nvar\n  i: integer;\nbegin\n  case i of\n    1..2, 5:\n      begin\n"
                "      end;\n  else\n    i := 1;\n    i := 2\n  end;\n  case i of\n    1:\n"
                "      i := 0;\n  else\n  end\nend.\n");
    expect_twin(L,
                "program t;var i:integer;begin for i:=1 to 2 do begin if i=1 then continue;break "
                "end;while true do break;repeat continue until true;exit end.",
                "program t;\nvar\n  i: integer;\nbegin\n  for i := 1 to 2 do\n    begin\n"
                "      if i = 1 then\n        continue;\n      break\n    end;\n  while true do\n"
                "    break;\n  repeat\n    continue\n  until true;\n  exit\nend.\n");
    expect_twin(L, "program t;const k=1;var a:integer;procedure p();begin end;const j=2;begin p() end.",
                "program t;\nconst\n  k = 1;\nvar\n  a: integer;\nprocedure p;\nbegin\nend;\n"
                "const\n  j = 2;\nbegin\n  p\nend.\n");
    expect_twin(L, "program t;var b:boolean;begin b:=not(1 and 2=0);b:=-(-1)<>(not 1) end.",
                "program t;\nvar\n  b: boolean;\nbegin\n  b := not (1 and 2 = 0);\n"
                "  b := -(-1) <> not 1\nend.\n");
    expect_twin(L, "// a } brace\nprogram t; { b } begin (* c *) end.",
                "// a } brace\nprogram t;\n{ b }\nbegin\n{ c }\nend.\n");
    {
        zl_diag_t d;
        const char *canon = "program t;\nbegin\n  writeln(1)\nend.\n";
        CHECK(zl_compile(L | ZL_TWIN, canon, strlen(canon), &P, &work, &d) == ZL_OK,
              "canonical Formwyrm is accepted: %s", d.msg);
        CHECK(P.word_bits == 32, "Delphi programs are 32-bit (%u)", P.word_bits);
        CHECK(strcmp(zl_twin_name(L), "Formwyrm") == 0, "the twin is Formwyrm");
        static const char *const off[] = {
            "program t;\nbegin\n  WriteLn(1)\nend.\n",
            "program t;\nbegin\n  writeln(1);\nend.\n",
            "program t;\nbegin\n  writeln(+1)\nend.\n",
            "program t;\nbegin\n  writeln($1)\nend.\n",
            "program t;\nprocedure p();\nbegin\nend;\nbegin\nend.\n",
            "program t;\nvar\n  x: longint;\nbegin\nend.\n",
            "program t;\nbegin\n  writeln(2 * -3)\nend.\n",
        };
        for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) {
            int s = zl_compile(L | ZL_TWIN, off[i], strlen(off[i]), &P, &work, &d);
            CHECK(s == ZL_E_NONCANON, "non-canonical Formwyrm %zu refused (%d)", i, s);
            CHECK(zl_compile(L, off[i], strlen(off[i]), &P, &work, &d) == ZL_OK,
                  "but it is Delphi %zu: %s", i, d.msg);
        }
    }
}

static void test_delphi_refusals(void)
{
    const int L = ZL_LANG_DELPHI;
    const int U = ZL_E_UNSUPPORTED, S = ZL_E_SYNTAX;
    expect_refused(L, "program t;\nuses SysUtils;\nbegin\nend.\n", U, 2, "uses");
    expect_refused(L, "unit u;\ninterface\nend.\n", U, 1, "units");
    expect_refused(L, DD("type T = class end;", ""), U, 2, "type declarations");
    expect_refused(L, DD("var o: TObject;", ""), ZL_E_UNDEFINED, 2, "an unknown type");
    expect_refused(L, DS("try WriteLn except end"), U, 3, "exceptions");
    expect_refused(L, DS("raise E"), U, 3, "raise");
    expect_refused(L, DD("var s: string;", ""), U, 2, "string variable");
    expect_refused(L, DD("var c: Char;", ""), U, 2, "Char variable");
    expect_refused(L, DS("WriteLn(#65)"), U, 3, "character codes");
    expect_refused(L, DS("WriteLn(Length('ab'))"), U, 3, "string functions");
    expect_refused(L, DS("WriteLn(IntToStr(1))"), U, 3, "IntToStr");
    expect_refused(L, DD("var r: Double;", ""), U, 2, "Double");
    expect_refused(L, DD("var r: Real;", ""), U, 2, "Real");
    expect_refused(L, DS("WriteLn(1.5)"), U, 3, "real literal");
    expect_refused(L, DS("WriteLn(4 / 2)"), U, 3, "real division");
    expect_refused(L, DD("var x: Int64;", ""), U, 2, "Int64");
    expect_refused(L, DD("var x: Cardinal;", ""), U, 2, "Cardinal");
    expect_refused(L, DD("var x: Byte;", ""), U, 2, "Byte");
    expect_refused(L, DS("WriteLn(High(Integer))"), U, 3, "High");
    expect_refused(L, DS("WriteLn(SizeOf(Integer))"), U, 3, "SizeOf");
    expect_refused(L, DS("Halt"), U, 3, "Halt");
    expect_refused(L, DS("WriteLn(1 shl 2)"), U, 3, "shl");
    expect_refused(L, DS("WriteLn($100000000)"), U, 3, "hex literal past MaxInt");
    expect_refused(L, DS("WriteLn(2147483648)"), U, 3, "literal past MaxInt");
    expect_refused(L, DD("procedure P(const x: Integer);\nbegin end;", ""), U, 2,
                   "const parameters");
    expect_refused(L, DD("procedure P(out x: Integer);\nbegin end;", ""), U, 2, "out parameters");
    expect_refused(L, DD("procedure P(x: Integer = 1);\nbegin end;", ""), U, 2,
                   "default parameter values");
    expect_refused(L, DD("procedure P; overload;\nbegin end;", ""), U, 2, "procedure directives");
    expect_refused(L, DD("var x: Integer = 1;", ""), U, 2, "initialised variables");
    expect_refused(L, DD("const x: Integer = 1;", ""), U, 2, "typed constants");
    expect_refused(L, DD("function F: Integer;\nbegin F := 1 end;", "F"), U, 5,
                   "a function called as a statement");
    expect_refused(L, "{$Q-}\nprogram t;\nbegin\nend.\n", U, 1, "{$Q-}");
    expect_refused(L, "program t;\n{$R-}\nbegin\nend.\n", U, 2, "{$R-}");
    expect_refused(L, "{$mode objfpc}\nprogram t;\nbegin\nend.\n", U, 1, "other modes");
    expect_refused(L, DS("asm end"), U, 3, "asm");
    expect_refused(L, DS("with x do WriteLn"), U, 3, "with");
    expect_refused(L, DS("goto 10"), U, 3, "goto");
    expect_refused(L, DD("var a: array[1..2, 1..2] of Integer;", ""), U, 2, "2-D array");
    expect_refused(L, DD("var p: ^Integer;", ""), U, 2, "pointers");
    expect_refused(L, DD("var x: Integer;", "x := 1;\nWriteLn(1:x)"), U, 5, "variable width");
    expect_refused(L, DS("case True of True: WriteLn end"), U, 3, "Boolean case selector");
    expect_refused(L, DS("WriteLn(1 in [1])"), U, 3, "in");
    /* syntax and type errors */
    expect_refused(L, DS("Break"), S, 3, "Break outside a loop");
    expect_refused(L, DS("Continue"), S, 3, "Continue outside a loop");
    expect_refused(L, DD("procedure P;\nbegin Exit(1) end;", ""), S, 3,
                   "Exit with a value in a procedure");
    expect_refused(L, DD("function F: Integer;\nbegin Exit(True) end;", ""), S, 3,
                   "Exit with a value of the wrong type");
    expect_refused(L, DS("WriteLn(1 and True)"), S, 3, "and of an integer and a Boolean");
    expect_refused(L, DD("var i: Integer;", "for i := 1 to 2 do Inc(i)"), S, 4,
                   "Inc of a for loop's control variable");
    expect_refused(L, DD("var b: Boolean;", "Inc(b)"), S, 4, "Inc of a Boolean");
    expect_refused(L, DD("var x: Integer;", "Inc(3)"), S, 4, "Inc of a constant");
    expect_refused(L, DS("case 1 of 1: WriteLn; 1: WriteLn end"), S, 3, "a label used twice");
    expect_refused(L, DS("case 1 of 1..3: WriteLn; 3..4: WriteLn end"), S, 3,
                   "overlapping ranges");
    expect_refused(L, DS("case 1 of 3..1: WriteLn end"), S, 3, "an empty range");
    expect_refused(L, DS("case 1 of else WriteLn end"), S, 3, "else with no arm");
    expect_refused(L, DD("function F: Integer;\nvar Result: Integer;\nbegin end;", ""), S, 3,
                   "Result declared again");
    expect_refused(L, DS("Result := 1"), ZL_E_UNDEFINED, 3, "Result outside a function");
    expect_refused(L, DS("WriteLn(1"), S, 4, "unclosed WriteLn");
}

static void test_delphi_traps(void)
{
    const int L = ZL_LANG_DELPHI;
    expect_trap(L, DD("var a: array[1..3] of Integer; i: Integer;", "i := 4;\na[i] := 1"),
                ZL_E_BOUNDS, 5, "index past the end");
    expect_trap(L, DD("var a: array[1..3] of Integer; i: Integer;", "i := 0;\nWriteLn(a[i])"),
                ZL_E_BOUNDS, 5, "index before the start");
    expect_trap(L,
                DD("procedure P;\nvar a: array[0..1] of Integer; i: Integer;\n"
                   "begin i := 2; Inc(a[i]) end;",
                   "P"),
                ZL_E_BOUNDS, 4, "Inc of an element outside a local array");
    expect_trap(L, DD("function F(n: Integer): Integer;\nbegin Result := F(n + 1) end;", "WriteLn(F(0))"),
                ZL_E_RSTACK, 0, "endless recursion");
    expect_trap(L, DS("while True do"), ZL_E_FUEL, 0, "an endless loop");
}

static void test_delphi(void)
{
    test_delphi_output();
    test_delphi_arith();
    test_delphi_statements();
    test_delphi_procs();
    test_delphi_read();
    test_delphi_twin();
    test_delphi_refusals();
    test_delphi_traps();
}
