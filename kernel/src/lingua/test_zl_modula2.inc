/* test_zl_modula2.inc — Modula-2 (Moduledrake) front-end tests, included by
 * test_zl.c. Expected outputs are what gm2 13 -fpim4 -fsoft-check-all
 * -fno-wholevalue prints for the same program; each trap is a run-time
 * error there too, except the ones marked [ref differs]: gm2 wraps on
 * overflow (its whole-value check is off, see ref_modula2.sh) and divides
 * by a negative divisor, which PIM leaves undefined and Lingua traps. */

/* MS: the body starts on line 4; MD: the declarations are line 3 and the
 * body line 5. */
#define M2HEAD         "MODULE t;\nFROM InOut IMPORT WriteInt, WriteString, WriteLn;\n"
#define MS(body)       M2HEAD "BEGIN\n" body "\nEND t.\n"
#define MD(decl, body) M2HEAD decl "\nBEGIN\n" body "\nEND t.\n"

static void test_modula2_output(void)
{
    const int L = ZL_LANG_MODULA2;
    expect_out(L, MS("WriteInt(5, 4); WriteInt(-5, 0); WriteInt(123, 1); WriteInt(-7, 3)"),
               "   5-5123 -7", "WriteInt pads to its width and never cuts");
    expect_out(L, MS("WriteString(\"it's\"); WriteString('say \"hi\"'); WriteString(\"\"); WriteLn"),
               "it'ssay \"hi\"\n", "WriteString with either quote");
    expect_out(L, MS("WriteLn; WriteString(\"a\"); WriteLn; WriteLn"), "\na\n\n", "WriteLn");
    expect_out(L, MS("WriteInt(MAX(INTEGER), 0); WriteInt(MIN(INTEGER), 12)"),
               "2147483647 -2147483648", "MAX(INTEGER) and MIN(INTEGER)");
    expect_out(L, MD("CONST w = 4; k = -2; yes = TRUE;", "WriteInt(k, w); IF yes THEN WriteString(\"y\") END"),
               "  -2y", "constants, a constant width");
}

static void test_modula2_arith(void)
{
    const int L = ZL_LANG_MODULA2;
    expect_out(L,
               MS("WriteInt(2 + 3 * 4, 3); WriteInt((2 + 3) * 4, 3); WriteInt(10 - 4 - 3, 3);\n"
                  "WriteInt(100 DIV 10 DIV 2, 3); WriteInt(2 * 3 MOD 4, 3)"),
               " 14 20  3  5  2", "precedence and left association");
    expect_out(L,
               MD("VAR x, y: INTEGER;",
                  "x := -7; y := 2;\nWriteInt(x DIV y, 3); WriteInt(x MOD y, 3); WriteInt(7 DIV y, 3);\n"
                  "WriteInt(7 MOD y, 3); WriteInt(-7 DIV 2, 3); WriteInt(x MOD 7, 3); WriteInt((-8) DIV 4, 3)"),
               " -4  1  3  1 -3  0 -2", "DIV rounds down, MOD is never negative, a sign takes the term");
    expect_out(L, MD("VAR x: INTEGER;", "x := -4;\nWriteInt(ABS(x), 2); WriteInt(ABS(3), 2)"), " 4 3",
               "ABS");
    expect_out(L,
               MS("IF ODD(3) & ODD(-3) & ~ODD(0) & NOT ODD(-4) THEN WriteString(\"odd\") END"),
               "odd", "ODD, & and AND, ~ and NOT");
    expect_out(L,
               MD("VAR z: INTEGER; b: BOOLEAN;",
                  "z := 0;\nb := (z > 0) AND (1 DIV z > 0);\nIF NOT b THEN WriteString(\"f\") END;\n"
                  "b := (z = 0) OR (1 MOD z = 0);\nIF b THEN WriteString(\"t\") END"),
               "ft", "AND and OR stop early");
    expect_out(L,
               MD("VAR n: INTEGER;",
                  "n := 0;\nIF 1 < 2 THEN INC(n) END; IF 2 <= 2 THEN INC(n, 10) END;\n"
                  "IF 3 > 4 THEN INC(n, 100) END; IF 4 >= 5 THEN INC(n, 1000) END;\n"
                  "IF 5 = 5 THEN INC(n, 10000) END; IF 6 # 6 THEN DEC(n) END; IF 6 <> 7 THEN DEC(n, 2) "
                  "END;\nIF FALSE < TRUE THEN INC(n, 5) END;\nWriteInt(n, 0)"),
               "10014", "comparisons, # and <>");
    expect_out(L, MD("VAR x: INTEGER;", "x := 46340;\nWriteInt(x * x, 0)"), "2147395600",
               "INTEGER is 32 bits");
    expect_trap(L, MD("VAR x: INTEGER;", "x := MAX(INTEGER);\nx := x + 1"), ZL_E_OVERFLOW, 6,
                "[ref differs] an INTEGER past MAX(INTEGER)");
    expect_trap(L, MD("VAR x: INTEGER;", "x := MIN(INTEGER);\nx := x - 1"), ZL_E_OVERFLOW, 6,
                "[ref differs] an INTEGER below MIN(INTEGER)");
    expect_trap(L, MD("VAR x: INTEGER;", "x := 65536;\nx := x * 32768"), ZL_E_OVERFLOW, 6,
                "[ref differs] a product past 32 bits");
    expect_trap(L, MD("VAR x: INTEGER;", "x := MIN(INTEGER);\nx := ABS(x)"), ZL_E_OVERFLOW, 6,
                "[ref differs] ABS of MIN(INTEGER)");
    expect_trap(L, MD("VAR x: INTEGER;", "x := MAX(INTEGER);\nINC(x)"), ZL_E_OVERFLOW, 6,
                "[ref differs] INC past MAX(INTEGER)");
    expect_trap(L, MD("VAR z: INTEGER;", "z := 0;\nWriteInt(1 DIV z, 0)"), ZL_E_DIVZERO, 6,
                "DIV by zero");
    expect_trap(L, MD("VAR z: INTEGER;", "z := 0;\nWriteInt(1 MOD z, 0)"), ZL_E_DIVZERO, 6,
                "MOD by zero");
    expect_trap(L, MD("VAR m: INTEGER;", "m := -2;\nWriteInt(5 DIV m, 0)"), ZL_E_DOMAIN, 6,
                "[ref differs] DIV by a negative divisor");
    expect_trap(L, MD("VAR m: INTEGER;", "m := -2;\nWriteInt(5 MOD m, 0)"), ZL_E_DOMAIN, 6,
                "[ref differs] MOD by a negative divisor");
}

static void test_modula2_statements(void)
{
    const int L = ZL_LANG_MODULA2;
    expect_out(L,
               MD("VAR i: INTEGER;",
                  "FOR i := 1 TO 4 DO\nIF i = 1 THEN WriteString(\"one\")\nELSIF i = 2 THEN "
                  "WriteString(\"two\")\nELSIF i = 3 THEN\nELSE WriteString(\"many\") END;\nWriteString(\".\")\nEND"),
               "one.two..many.", "IF, ELSIF, an empty arm, ELSE");
    expect_out(L,
               MD("VAR i, s: INTEGER;", "i := 0; s := 0;\nWHILE i < 10 DO INC(i); s := s + i END;\nWriteInt(s, 0)"),
               "55", "WHILE");
    expect_out(L, MD("VAR i: INTEGER;", "i := 0;\nREPEAT i := i + 2; WriteInt(i, 2) UNTIL i >= 6"),
               " 2 4 6", "REPEAT");
    expect_out(L,
               MD("VAR i: INTEGER;",
                  "FOR i := 1 TO 4 DO WriteInt(i, 0) END;\nFOR i := -1 TO -7 BY -3 DO WriteInt(i, 3) END;\n"
                  "FOR i := 2 TO 1 DO WriteString(\"x\") END;\nFOR i := 1 TO 9 BY 4 DO WriteInt(i, 2) END;\n"
                  "WriteInt(i, 2)"),
               "1234 -1 -4 -7 1 5 9 9", "FOR with BY; the variable keeps its last value");
    expect_out(L,
               MD("VAR i, n: INTEGER;", "n := 3;\nFOR i := 1 TO n DO n := 10; WriteInt(i, 0) END"),
               "123", "the FOR limit is evaluated once");
    expect_out(L,
               MD("VAR i: INTEGER;",
                  "FOR i := MAX(INTEGER) - 1 TO MAX(INTEGER) DO WriteInt(i - MAX(INTEGER), 0) END;\n"
                  "FOR i := MIN(INTEGER) + 1 TO MIN(INTEGER) BY -1 DO WriteInt(i - MIN(INTEGER), 0) END"),
               "-1010", "FOR to MAX(INTEGER) or MIN(INTEGER) does not step past it");
    expect_out(L,
               MD("VAR i: INTEGER;",
                  "i := 0;\nLOOP\nINC(i);\nIF i > 5 THEN EXIT END;\nIF ODD(i) THEN ELSE WriteInt(i, 0) END\nEND"),
               "24", "LOOP and EXIT");
    expect_out(L,
               MD("VAR i, j: INTEGER;",
                  "FOR i := 1 TO 3 DO\nj := 0;\nLOOP INC(j); IF j = 2 THEN EXIT END; WriteInt(i, 0); "
                  "WriteInt(j, 0) END;\nWriteString(\".\")\nEND"),
               "11.21.31.", "EXIT leaves the innermost LOOP");
    expect_out(L,
               MD("VAR i: INTEGER;",
                  "FOR i := 0 TO 7 DO\nCASE i OF\n1..3, 5: WriteString(\"a\")\n| 0: WriteString(\"z\")\n"
                  "| 6: WriteString(\"s\")\n|\nELSE WriteString(\"e\"); WriteString(\".\")\nEND\nEND"),
               "zaaae.ase.", "CASE with ranges, an empty arm and ELSE");
    expect_out(L, MD("VAR i: INTEGER;", "i := -4;\nCASE i OF -5..-3: WriteString(\"n\") | 0: END"), "n",
               "negative labels");
    expect_out(L,
               MD("VAR i: INTEGER; a: ARRAY [-1..1] OF INTEGER;",
                  "i := 5; INC(i); INC(i, 10); DEC(i); DEC(i, 3);\na[0] := 1; INC(a[0], 4);\n"
                  "WriteInt(i, 3); WriteInt(a[0], 2)"),
               " 12 5", "INC and DEC");
    expect_out(L, MS(";;\nWriteString(\"a\");;\n"), "a", "empty statements");
    expect_out(L, MD("VAR abc, Abc: INTEGER;", "abc := 1; Abc := 2;\nWriteInt(abc, 0); WriteInt(Abc, 0)"),
               "12", "names are case-sensitive");
    expect_out(L, MS("(* a (* nested *) comment *) WriteString(\"c\")"), "c", "nested comments");
    expect_out(L,
               MD("VAR a: ARRAY [-2..2] OF INTEGER; b: ARRAY [0..1] OF BOOLEAN; i: INTEGER;",
                  "FOR i := -2 TO 2 DO a[i] := i * i END;\nb[1] := a[-2] = 4;\n"
                  "IF b[1] & ~b[0] THEN WriteInt(a[2], 0) END"),
               "4", "arrays of INTEGER and BOOLEAN with any bounds");
}

static void test_modula2_procs(void)
{
    const int L = ZL_LANG_MODULA2;
    expect_out(L,
               MD("PROCEDURE F(n: INTEGER): INTEGER;\nBEGIN\nIF n < 2 THEN RETURN 1 END;\n"
                  "RETURN n * F(n - 1)\nEND F;",
                  "WriteInt(F(10), 0)"),
               "3628800", "RETURN and recursion");
    expect_out(L,
               MD("PROCEDURE G(n: INTEGER): INTEGER;\nVAR i: INTEGER;\nBEGIN\nFOR i := 1 TO 10 DO\n"
                  "IF i * i > n THEN RETURN i END\nEND;\nRETURN 0\nEND G;",
                  "WriteInt(G(50), 3); WriteInt(G(500), 3)"),
               "  8  0", "RETURN from inside a loop");
    expect_out(L,
               MD("VAR a, b: INTEGER;\nPROCEDURE Swap(VAR x, y: INTEGER);\nVAR t: INTEGER;\n"
                  "BEGIN t := x; x := y; y := t END Swap;",
                  "a := 1; b := 2;\nSwap(a, b);\nWriteInt(a, 0); WriteInt(b, 0)"),
               "21", "VAR parameters");
    expect_out(L,
               MD("PROCEDURE P();\nBEGIN WriteString(\"p\") END P;\nPROCEDURE H(): INTEGER;\n"
                  "BEGIN RETURN 4 END H;",
                  "P(); P;\nWriteInt(H(), 0)"),
               "pp4", "calls with and without parentheses");
    expect_out(L,
               MD("PROCEDURE P(n: INTEGER);\nBEGIN\nIF n > 2 THEN RETURN END;\nWriteInt(n, 0);\n"
                  "P(n + 1)\nEND P;",
                  "P(0)"),
               "012", "RETURN in a proper procedure");
    expect_out(L,
               MD("PROCEDURE Outer(n: INTEGER): INTEGER;\nVAR s: INTEGER;\n"
                  "  PROCEDURE Add(k: INTEGER);\n  BEGIN s := s + k * n END Add;\n"
                  "BEGIN s := 0; Add(1); Add(2); RETURN s END Outer;",
                  "WriteInt(Outer(5), 0)"),
               "15", "a nested procedure reaches its parent's variables");
    expect_out(L,
               MD("VAR g: INTEGER;\nPROCEDURE A;\nBEGIN INC(g) END A;\nCONST k = 5;\nVAR h: INTEGER;\n"
                  "PROCEDURE B(): INTEGER;\nBEGIN RETURN k + g END B;",
                  "A; h := B();\nWriteInt(h, 0)"),
               "6", "declarations in any order");
    expect_out(L,
               MD("VAR v: ARRAY [1..3] OF INTEGER;\nPROCEDURE Dbl(VAR x: INTEGER);\n"
                  "BEGIN x := x * 10 END Dbl;",
                  "v[2] := 4;\nDbl(v[2]);\nWriteInt(v[2], 0)"),
               "40", "an array element as a VAR argument");
    expect_out(L,
               MD("PROCEDURE Even(n: INTEGER): BOOLEAN;\nBEGIN RETURN n MOD 2 = 0 END Even;",
                  "IF Even(4) & ~Even(3) THEN WriteString(\"ok\") END"),
               "ok", "a BOOLEAN function");
    expect_trap(L,
                MD("PROCEDURE F(n: INTEGER): INTEGER;\nBEGIN\nIF n > 0 THEN RETURN n END\nEND F;",
                   "WriteInt(F(-1), 0)"),
                ZL_E_DOMAIN, 6, "a function that ends without RETURN");
}

static void test_modula2_twin(void)
{
    const int L = ZL_LANG_MODULA2;
    expect_twin(L, "MODULE Hello;FROM InOut IMPORT WriteString,WriteLn;BEGIN WriteString('Hi');WriteLn END Hello.",
                "MODULE Hello;\nFROM InOut IMPORT WriteString, WriteLn;\nBEGIN\n  WriteString(\"Hi\");\n"
                "  WriteLn\nEND Hello.\n");
    expect_twin(L,
                "MODULE t;CONST k=-1;VAR x:INTEGER;b:BOOLEAN;(* c *)\nPROCEDURE F(VAR y:INTEGER):INTEGER;"
                "BEGIN INC(y,1);RETURN y END F;BEGIN x:=F(x);b:=~(x<>k)&b;"
                "IF b THEN ELSIF x=1 THEN x:=2 ELSE END;FOR x:=1 TO 2 BY 1 DO END END t.",
                "MODULE t;\nCONST\n  k = -1;\nVAR\n  x: INTEGER;\n  b: BOOLEAN;\n(* c *)\n"
                "PROCEDURE F(VAR y: INTEGER): INTEGER;\nBEGIN\n  INC(y, 1);\n  RETURN y\nEND F;\n"
                "BEGIN\n  x := F(x);\n  b := NOT (x # k) AND b;\n  IF b THEN\n  ELSIF x = 1 THEN\n"
                "    x := 2\n  ELSE\n  END;\n  FOR x := 1 TO 2 DO\n  END\nEND t.\n");
    expect_twin(L,
                "MODULE t;VAR i:INTEGER;BEGIN CASE i OF 1..2,5:|3:i:=0 ELSE i:=1;i:=2 END;"
                "LOOP EXIT END;REPEAT UNTIL TRUE;WHILE FALSE DO END END t.",
                "MODULE t;\nVAR\n  i: INTEGER;\nBEGIN\n  CASE i OF\n  1..2, 5:\n  | 3:\n    i := 0\n"
                "  ELSE\n    i := 1;\n    i := 2\n  END;\n  LOOP\n    EXIT\n  END;\n  REPEAT\n"
                "  UNTIL TRUE;\n  WHILE FALSE DO\n  END\nEND t.\n");
    {
        zl_diag_t d;
        const char *canon = "MODULE t;\nBEGIN\nEND t.\n";
        CHECK(zl_compile(L | ZL_TWIN, canon, strlen(canon), &P, &work, &d) == ZL_OK,
              "canonical Moduledrake is accepted: %s", d.msg);
        CHECK(P.word_bits == 32, "Modula-2 programs are 32-bit (%u)", P.word_bits);
        CHECK(strcmp(zl_twin_name(L), "Moduledrake") == 0, "the twin is Moduledrake");
        static const char *const off[] = {
            "MODULE t;\nBEGIN\nEND t.",
            "MODULE t;\nBEGIN\n;\nEND t.\n",
            "MODULE t;\nVAR\n  x: INTEGER;\nBEGIN\n  x := 1;\nEND t.\n",
            "MODULE t;\nVAR\n  b: BOOLEAN;\nBEGIN\n  b := b & b\nEND t.\n",
            "MODULE t;\nVAR\n  b: BOOLEAN;\nBEGIN\n  b := 1 <> 2\nEND t.\n",
            "MODULE t;\nVAR\n  x: INTEGER;\nBEGIN\n  FOR x := 1 TO 2 BY 1 DO\n  END\nEND t.\n",
            "MODULE t;\nPROCEDURE P();\nBEGIN\nEND P;\nBEGIN\nEND t.\n",
        };
        for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) {
            int s = zl_compile(L | ZL_TWIN, off[i], strlen(off[i]), &P, &work, &d);
            CHECK(s == ZL_E_NONCANON, "non-canonical Moduledrake %zu refused (%d)", i, s);
            CHECK(zl_compile(L, off[i], strlen(off[i]), &P, &work, &d) == ZL_OK,
                  "but it is Modula-2 %zu: %s", i, d.msg);
        }
    }
}

static void test_modula2_refusals(void)
{
    const int L = ZL_LANG_MODULA2;
    const int U = ZL_E_UNSUPPORTED, S = ZL_E_SYNTAX;
    expect_refused(L, "MODULE t;\nIMPORT InOut;\nBEGIN\nEND t.\n", U, 2, "IMPORT of a whole module");
    expect_refused(L, "MODULE t;\nFROM StrIO IMPORT WriteString;\nBEGIN\nEND t.\n", U, 2,
                   "another module");
    expect_refused(L, "MODULE t;\nFROM InOut IMPORT ReadInt;\nBEGIN\nEND t.\n", U, 2,
                   "other InOut procedures");
    expect_refused(L, "DEFINITION MODULE t;\nEND t.\n", U, 1, "definition modules");
    expect_refused(L, "IMPLEMENTATION MODULE t;\nBEGIN\nEND t.\n", U, 1, "implementation modules");
    expect_refused(L, MD("VAR c: CARDINAL;", ""), U, 3, "CARDINAL");
    expect_refused(L, MD("VAR c: LONGINT;", ""), U, 3, "LONGINT");
    expect_refused(L, MD("VAR c: CHAR;", ""), U, 3, "CHAR");
    expect_refused(L, MD("VAR r: REAL;", ""), U, 3, "REAL");
    expect_refused(L, MD("VAR s: BITSET;", ""), U, 3, "BITSET");
    expect_refused(L, MD("TYPE T = INTEGER;", ""), U, 3, "TYPE");
    expect_refused(L, MD("VAR r: RECORD a: INTEGER END;", ""), U, 3, "RECORD");
    expect_refused(L, MD("VAR s: SET OF [0..3];", ""), U, 3, "SET");
    expect_refused(L, MD("VAR p: POINTER TO INTEGER;", ""), U, 3, "POINTER");
    expect_refused(L, MD("VAR a: ARRAY [1..2], [1..2] OF INTEGER;", ""), U, 3, "2-D arrays");
    expect_refused(L, MD("VAR a: ARRAY [1..2] OF ARRAY [1..2] OF INTEGER;", ""), U, 3,
                   "arrays of arrays");
    expect_refused(L, MD("PROCEDURE P(a: ARRAY OF INTEGER);\nBEGIN END P;", ""), U, 3,
                   "open array parameters");
    expect_refused(L, MD("VAR p: PROCEDURE;", ""), U, 3, "procedure variables");
    expect_refused(L, MD("CONST k = 1 + 2;", ""), U, 3, "constant expressions");
    expect_refused(L, MS("WriteInt(0FFH, 0)"), U, 4, "hexadecimal literals");
    expect_refused(L, MS("WriteInt(17B, 0)"), U, 4, "octal literals");
    expect_refused(L, MS("WriteInt(101C, 0)"), U, 4, "character literals");
    expect_refused(L, MS("WriteInt(1.5, 0)"), U, 4, "real literals");
    expect_refused(L, MS("WriteInt(4 / 2, 0)"), U, 4, "real division");
    expect_refused(L, MD("VAR my_x: INTEGER;", ""), U, 3, "underscores in names");
    expect_refused(L, MS("RETURN"), U, 4, "RETURN in the module body");
    expect_refused(L, MS("HALT"), U, 4, "HALT");
    expect_refused(L, MS("WriteInt(ORD(TRUE), 0)"), U, 4, "ORD");
    expect_refused(L, MS("WriteInt(CHR(65), 0)"), U, 4, "CHR");
    expect_refused(L, MS("WriteInt(HIGH(x), 0)"), U, 4, "HIGH");
    expect_refused(L, MS("WriteInt(SIZE(INTEGER), 0)"), U, 4, "SIZE");
    expect_refused(L, MS("WriteInt(MAX(BOOLEAN), 0)"), U, 4, "MAX of another type");
    expect_refused(L, MD("VAR x: INTEGER;", "WITH x DO END"), U, 5, "WITH");
    expect_refused(L, MS("IF 1 IN {1} THEN END"), U, 4, "IN and sets");
    expect_refused(L, MD("VAR x: INTEGER;", "x := 1;\nWriteInt(1, x)"), U, 6, "a variable width");
    expect_refused(L, MD("VAR b: BOOLEAN;", "CASE b OF TRUE: END"), U, 5, "a BOOLEAN CASE selector");
    /* syntax and type errors */
    expect_refused(L, MS("EXIT"), S, 4, "EXIT outside a LOOP");
    expect_refused(L, MD("PROCEDURE P;\nBEGIN END Q;", ""), S, 4, "END with the wrong name");
    expect_refused(L, "MODULE t;\nBEGIN\nEND u.\n", S, 3, "the module's END with the wrong name");
    expect_refused(L, MD("PROCEDURE P;\nBEGIN RETURN 1 END P;", ""), S, 4,
                   "RETURN with a value in a proper procedure");
    expect_refused(L, MD("PROCEDURE F(): INTEGER;\nBEGIN RETURN END F;", ""), S, 4,
                   "RETURN without a value in a function");
    expect_refused(L, MD("PROCEDURE F(): INTEGER;\nBEGIN RETURN 1 END F;", "F()"), S, 6,
                   "a function called as a statement");
    expect_refused(L, MD("VAR b: BOOLEAN;", "INC(b)"), S, 5, "INC of a BOOLEAN");
    expect_refused(L, MD("VAR i: INTEGER;", "FOR i := 1 TO 2 BY 0 DO END"), S, 5, "BY 0");
    expect_refused(L, MD("VAR i, k: INTEGER;", "FOR i := 1 TO 2 BY k DO END"), U, 5,
                   "a BY that is not constant");
    expect_refused(L, MS("CASE 1 OF 1: | 1: END"), S, 4, "a label used twice");
    expect_refused(L, MS("CASE 1 OF 1..3: | 3..4: END"), S, 4, "overlapping ranges");
    expect_refused(L, MS("WriteInt(1 AND TRUE, 0)"), S, 4, "AND of an INTEGER");
    expect_refused(L, MS("WriteInt(TRUE, 0)"), S, 4, "WriteInt of a BOOLEAN");
    expect_refused(L, "module t;\nbegin\nend t.\n", S, 1, "keywords are upper case");
    expect_refused(L, MD("VAR x: INTEGER;", "X := 1"), ZL_E_UNDEFINED, 5, "names are case-sensitive");
    expect_refused(L, MS("WriteString(\"a\""), S, 5, "an unclosed call");
}

static void test_modula2_traps(void)
{
    const int L = ZL_LANG_MODULA2;
    expect_trap(L, MD("VAR a: ARRAY [1..3] OF INTEGER; i: INTEGER;", "i := 4;\na[i] := 1"),
                ZL_E_BOUNDS, 6, "an index past the end");
    expect_trap(L, MD("VAR a: ARRAY [1..3] OF INTEGER; i: INTEGER;", "i := 0;\nWriteInt(a[i], 0)"),
                ZL_E_BOUNDS, 6, "an index before the start");
    expect_trap(L, MD("VAR i: INTEGER;", "i := 9;\nCASE i OF 1: WriteString(\"a\") | 2: END"), ZL_E_DOMAIN,
                6, "CASE with no matching label and no ELSE");
    expect_trap(L, MD("PROCEDURE F(n: INTEGER): INTEGER;\nBEGIN RETURN F(n + 1) END F;", "WriteInt(F(0), 0)"),
                ZL_E_RSTACK, 0, "endless recursion");
    /* an empty LOOP END hangs gm2 13 itself while compiling */
    expect_trap(L, MD("VAR i: INTEGER;", "LOOP INC(i, 0) END"), ZL_E_FUEL, 0, "an endless LOOP");
}

static void test_modula2(void)
{
    test_modula2_output();
    test_modula2_arith();
    test_modula2_statements();
    test_modula2_procs();
    test_modula2_twin();
    test_modula2_refusals();
    test_modula2_traps();
}
