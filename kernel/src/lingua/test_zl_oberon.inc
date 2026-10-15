/* test_zl_oberon.inc — Oberon (Faydrake) front-end tests, included by
 * test_zl.c. Expected outputs are what OBNC 0.17 prints for the same
 * program (checked by hand where OBNC is installed; CI checks Oberon
 * through gm2, see ref_oberon.sh); each trap is a run-time error there
 * too, except the ones marked [ref differs]: OBNC wraps on overflow and
 * divides by a negative divisor, which the Oberon-07 report leaves
 * undefined and Lingua traps. */

/* OS: the body starts on line 4; OD: the declarations are line 3 and the
 * body line 5. */
#define OS(body)       "MODULE t;\nIMPORT Out;\nBEGIN\n" body "\nEND t.\n"
#define OD(decl, body) "MODULE t;\nIMPORT Out;\n" decl "\nBEGIN\n" body "\nEND t.\n"

static void test_oberon_output(void)
{
    const int L = ZL_LANG_OBERON;
    expect_out(L, OS("Out.Int(5, 4); Out.Int(-5, 0); Out.Int(123, 1); Out.Int(-7, 3)"),
               "   5-5123 -7", "Out.Int pads to its width and never cuts");
    expect_out(L, OS("Out.String(\"it's\"); Out.String(\"\"); Out.Char(\"x\"); Out.Ln"), "it'sx\n",
               "Out.String, Out.Char and Out.Ln");
    expect_out(L, OS("Out.Ln; Out.String(\"a\"); Out.Ln; Out.Ln"), "\na\n\n", "Out.Ln");
    expect_out(L, OS("Out.Int(2147483647, 0); Out.Int(-2147483647 - 1, 12)"),
               "2147483647 -2147483648", "the largest and least INTEGER");
    expect_out(L, OD("CONST w = 4; k = -2; yes = TRUE;", "Out.Int(k, w); IF yes THEN Out.String(\"y\") END"),
               "  -2y", "constants, a constant width");
}

static void test_oberon_arith(void)
{
    const int L = ZL_LANG_OBERON;
    expect_out(L,
               OS("Out.Int(2 + 3 * 4, 3); Out.Int((2 + 3) * 4, 3); Out.Int(10 - 4 - 3, 3);\n"
                  "Out.Int(100 DIV 10 DIV 2, 3); Out.Int(2 * 3 MOD 4, 3)"),
               " 14 20  3  5  2", "precedence and left association");
    expect_out(L,
               OD("VAR x, y: INTEGER;",
                  "x := -7; y := 2;\nOut.Int(x DIV y, 3); Out.Int(x MOD y, 3); Out.Int(7 DIV y, 3);\n"
                  "Out.Int(7 MOD y, 3); Out.Int(-7 DIV 2, 3); Out.Int(x MOD 7, 3); Out.Int((-8) DIV 4, 3)"),
               " -4  1  3  1 -3  0 -2", "DIV rounds down, MOD is never negative, a sign takes the term");
    expect_out(L, OD("VAR x: INTEGER;", "x := -4;\nOut.Int(ABS(x), 2); Out.Int(ABS(3), 2)"), " 4 3", "ABS");
    expect_out(L, OS("IF ODD(3) & ODD(-3) & ~ODD(0) & ~ODD(-4) THEN Out.String(\"odd\") END"), "odd",
               "ODD, & and ~");
    expect_out(L,
               OD("VAR z: INTEGER; b: BOOLEAN;",
                  "z := 0;\nb := (z > 0) & (1 DIV z > 0);\nIF ~b THEN Out.String(\"f\") END;\n"
                  "b := (z = 0) OR (1 MOD z = 0);\nIF b THEN Out.String(\"t\") END"),
               "ft", "& and OR stop early");
    expect_out(L,
               OD("VAR n: INTEGER;",
                  "n := 0;\nIF 1 < 2 THEN INC(n) END; IF 2 <= 2 THEN INC(n, 10) END;\n"
                  "IF 3 > 4 THEN INC(n, 100) END; IF 4 >= 5 THEN INC(n, 1000) END;\n"
                  "IF 5 = 5 THEN INC(n, 10000) END; IF 6 # 6 THEN DEC(n) END; IF 6 # 7 THEN DEC(n, 2) "
                  "END;\nIF TRUE = TRUE THEN INC(n, 5) END;\nOut.Int(n, 0)"),
               "10014", "comparisons");
    expect_out(L, OD("VAR x: INTEGER;", "x := 46340;\nOut.Int(x * x, 0)"), "2147395600",
               "INTEGER is 32 bits");
    expect_trap(L, OD("VAR x: INTEGER;", "x := 2147483647;\nx := x + 1"), ZL_E_OVERFLOW, 6,
                "[ref differs] an INTEGER past the largest");
    expect_trap(L, OD("VAR x: INTEGER;", "x := -2147483647;\nx := x - 2"), ZL_E_OVERFLOW, 6,
                "[ref differs] an INTEGER below the least");
    expect_trap(L, OD("VAR x: INTEGER;", "x := 65536;\nx := x * 32768"), ZL_E_OVERFLOW, 6,
                "[ref differs] a product past 32 bits");
    expect_trap(L, OD("VAR x: INTEGER;", "x := 2147483647;\nINC(x)"), ZL_E_OVERFLOW, 6,
                "[ref differs] INC past the largest");
    /* OBNC 0.17 prints 0 for this one rather than stopping */
    expect_trap(L, OD("VAR z: INTEGER;", "z := 0;\nOut.Int(1 DIV z, 0)"), ZL_E_DIVZERO, 6,
                "[ref differs] DIV by zero");
    expect_trap(L, OD("VAR z: INTEGER;", "z := 0;\nOut.Int(1 MOD z, 0)"), ZL_E_DIVZERO, 6, "MOD by zero");
    expect_trap(L, OD("VAR m: INTEGER;", "m := -2;\nOut.Int(5 DIV m, 0)"), ZL_E_DOMAIN, 6,
                "[ref differs] DIV by a negative divisor");
    expect_trap(L, OD("VAR m: INTEGER;", "m := -2;\nOut.Int(5 MOD m, 0)"), ZL_E_DOMAIN, 6,
                "[ref differs] MOD by a negative divisor");
}

static void test_oberon_statements(void)
{
    const int L = ZL_LANG_OBERON;
    expect_out(L,
               OD("VAR i: INTEGER;",
                  "FOR i := 1 TO 4 DO\nIF i = 1 THEN Out.String(\"one\")\nELSIF i = 2 THEN "
                  "Out.String(\"two\")\nELSIF i = 3 THEN\nELSE Out.String(\"many\") END;\nOut.String(\".\")\nEND"),
               "one.two..many.", "IF, ELSIF, an empty arm, ELSE");
    expect_out(L,
               OD("VAR i, s: INTEGER;", "i := 0; s := 0;\nWHILE i < 10 DO INC(i); s := s + i END;\nOut.Int(s, 0)"),
               "55", "WHILE");
    expect_out(L,
               OD("VAR x, y: INTEGER;",
                  "x := 84; y := 36;\nWHILE x > y DO DEC(x, y) ELSIF y > x DO DEC(y, x) END;\nOut.Int(x, 0)"),
               "12", "WHILE with an ELSIF arm");
    expect_out(L, OD("VAR i: INTEGER;", "i := 0;\nREPEAT i := i + 2; Out.Int(i, 2) UNTIL i >= 6"),
               " 2 4 6", "REPEAT");
    expect_out(L,
               OD("VAR i: INTEGER;",
                  "FOR i := 1 TO 4 DO Out.Int(i, 0) END;\nFOR i := -1 TO -7 BY -3 DO Out.Int(i, 3) END;\n"
                  "FOR i := 2 TO 1 DO Out.String(\"x\") END;\nFOR i := 1 TO 9 BY 4 DO Out.Int(i, 2) END;\n"
                  "Out.Int(i, 3)"),
               "1234 -1 -4 -7 1 5 9 13", "FOR with BY; the variable ends past the limit");
    expect_out(L,
               OD("VAR i, n: INTEGER;", "n := 3;\nFOR i := 1 TO n DO n := 5; Out.Int(i, 0) END"),
               "12345", "the FOR limit is evaluated every time round");
    expect_out(L,
               OD("VAR i: INTEGER;", "FOR i := 1 TO 6 DO Out.Int(i, 0); i := i + 1 END"), "135",
               "the FOR variable may be assigned");
    expect_out(L,
               OD("VAR i: INTEGER;",
                  "FOR i := 0 TO 7 DO\nCASE i OF\n1..3, 5: Out.String(\"a\")\n| 0: Out.String(\"z\")\n"
                  "| 6: Out.String(\"s\")\n|\n| 4, 7: Out.String(\"e\")\nEND\nEND"),
               "zaaaease", "CASE with ranges and an empty arm");
    expect_out(L, OD("CONST lo = 2; hi = 4;\nVAR i: INTEGER;", "i := 3;\nCASE i OF lo..hi: Out.String(\"n\") | 0: END"),
               "n", "labels that name constants");
    expect_out(L,
               OD("VAR i: INTEGER; a: ARRAY 3 OF INTEGER;",
                  "i := 5; INC(i); INC(i, 10); DEC(i); DEC(i, 3);\na[0] := 1; INC(a[0], 4);\n"
                  "Out.Int(i, 3); Out.Int(a[0], 2)"),
               " 12 5", "INC and DEC");
    expect_out(L, OS(";;\nOut.String(\"a\");;\n"), "a", "empty statements");
    expect_out(L, OD("VAR abc, Abc: INTEGER;", "abc := 1; Abc := 2;\nOut.Int(abc, 0); Out.Int(Abc, 0)"),
               "12", "names are case-sensitive");
    expect_out(L, OS("(* a (* nested *) comment *) Out.String(\"c\")"), "c", "nested comments");
    expect_out(L,
               OD("CONST n = 5;\nVAR a: ARRAY n OF INTEGER; b: ARRAY 2 OF BOOLEAN; i: INTEGER;",
                  "FOR i := 0 TO n - 1 DO a[i] := i * i END;\nb[1] := a[2] = 4;\n"
                  "IF b[1] & ~b[0] THEN Out.Int(a[4], 0) END"),
               "16", "arrays of INTEGER and BOOLEAN, from 0");
}

static void test_oberon_procs(void)
{
    const int L = ZL_LANG_OBERON;
    expect_out(L,
               OD("PROCEDURE F(n: INTEGER): INTEGER;\nVAR r: INTEGER;\nBEGIN\nIF n < 2 THEN r := 1 "
                  "ELSE r := n * F(n - 1) END\nRETURN r\nEND F;",
                  "Out.Int(F(10), 0)"),
               "3628800", "RETURN and recursion");
    expect_out(L,
               OD("PROCEDURE G(): INTEGER;\nRETURN 7\nEND G;", "Out.Int(G(), 0)"), "7",
               "a function that is only RETURN");
    expect_out(L,
               OD("VAR a, b: INTEGER;\nPROCEDURE Swap(VAR x, y: INTEGER);\nVAR t: INTEGER;\n"
                  "BEGIN t := x; x := y; y := t END Swap;",
                  "a := 1; b := 2;\nSwap(a, b);\nOut.Int(a, 0); Out.Int(b, 0)"),
               "21", "VAR parameters");
    expect_out(L,
               OD("PROCEDURE P;\nBEGIN Out.String(\"p\") END P;", "P(); P"), "pp",
               "calls with and without parentheses");
    expect_out(L,
               OD("PROCEDURE P(n: INTEGER);\nBEGIN n := n * 2; Out.Int(n, 0) END P;", "P(4)"), "8",
               "a value parameter may be assigned");
    expect_out(L,
               OD("VAR g: INTEGER;\nPROCEDURE Outer(n: INTEGER): INTEGER;\n"
                  "  PROCEDURE Add(k: INTEGER);\n  BEGIN g := g + k END Add;\n"
                  "BEGIN g := 0; Add(n); Add(2 * n)\nRETURN g END Outer;",
                  "Out.Int(Outer(5), 0)"),
               "15", "a nested procedure reaches the module's variables");
    expect_out(L,
               OD("VAR v: ARRAY 3 OF INTEGER;\nPROCEDURE Dbl(VAR x: INTEGER);\n"
                  "BEGIN x := x * 10 END Dbl;",
                  "v[2] := 4;\nDbl(v[2]);\nOut.Int(v[2], 0)"),
               "40", "an array element as a VAR argument");
    expect_out(L,
               OD("PROCEDURE Even(n: INTEGER): BOOLEAN;\nRETURN n MOD 2 = 0\nEND Even;",
                  "IF Even(4) & ~Even(3) THEN Out.String(\"ok\") END"),
               "ok", "a BOOLEAN function");
}

static void test_oberon_twin(void)
{
    const int L = ZL_LANG_OBERON;
    expect_twin(L, "MODULE Hello;IMPORT Out;BEGIN Out.String(\"Hi\");Out.Ln END Hello.",
                "MODULE Hello;\nIMPORT Out;\nBEGIN\n  Out.String(\"Hi\");\n  Out.Ln\nEND Hello.\n");
    expect_twin(L,
                "MODULE t;CONST k=-1;VAR x:INTEGER;b:BOOLEAN;(* c *)\nPROCEDURE F(VAR y:INTEGER):INTEGER;"
                "BEGIN INC(y,1)RETURN y END F;BEGIN x:=F(x);b:=~(x#k)&b;"
                "IF b THEN ELSIF x=1 THEN x:=2 ELSE END;FOR x:=1 TO 2 BY 1 DO END END t.",
                "MODULE t;\nCONST\n  k = -1;\nVAR\n  x: INTEGER;\n  b: BOOLEAN;\n(* c *)\n"
                "PROCEDURE F(VAR y: INTEGER): INTEGER;\nBEGIN\n  INC(y, 1);\n  RETURN y\nEND F;\n"
                "BEGIN\n  x := F(x);\n  b := ~(x # k) & b;\n  IF b THEN\n  ELSIF x = 1 THEN\n"
                "    x := 2\n  ELSE\n  END;\n  FOR x := 1 TO 2 DO\n  END\nEND t.\n");
    expect_twin(L,
                "MODULE t;VAR i:INTEGER;BEGIN CASE i OF 1..2,5:|3:i:=0 END;"
                "WHILE i>0 DO DEC(i) ELSIF i<0 DO INC(i) END;REPEAT UNTIL TRUE END t.",
                "MODULE t;\nVAR\n  i: INTEGER;\nBEGIN\n  CASE i OF\n  1..2, 5:\n  | 3:\n    i := 0\n"
                "  END;\n  WHILE i > 0 DO\n    DEC(i)\n  ELSIF i < 0 DO\n    INC(i)\n  END;\n  REPEAT\n"
                "  UNTIL TRUE\nEND t.\n");
    {
        zl_diag_t d;
        const char *canon = "MODULE t;\nBEGIN\nEND t.\n";
        CHECK(zl_compile(L | ZL_TWIN, canon, strlen(canon), &P, &work, &d) == ZL_OK,
              "canonical Faydrake is accepted: %s", d.msg);
        CHECK(P.word_bits == 32, "Oberon programs are 32-bit (%u)", P.word_bits);
        CHECK(strcmp(zl_twin_name(L), "Faydrake") == 0, "the twin is Faydrake");
        static const char *const off[] = {
            "MODULE t;\nBEGIN\nEND t.",
            "MODULE t;\nBEGIN\n;\nEND t.\n",
            "MODULE t;\nVAR\n  x: INTEGER;\nBEGIN\n  x := 1;\nEND t.\n",
            "MODULE t;\nVAR\n  x: INTEGER;\nBEGIN\n  FOR x := 1 TO 2 BY 1 DO\n  END\nEND t.\n",
            "MODULE t;\nPROCEDURE P();\nBEGIN\nEND P;\nBEGIN\nEND t.\n",
        };
        for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) {
            int s = zl_compile(L | ZL_TWIN, off[i], strlen(off[i]), &P, &work, &d);
            CHECK(s == ZL_E_NONCANON, "non-canonical Faydrake %zu refused (%d)", i, s);
            CHECK(zl_compile(L, off[i], strlen(off[i]), &P, &work, &d) == ZL_OK,
                  "but it is Oberon %zu: %s", i, d.msg);
        }
    }
}

static void test_oberon_refusals(void)
{
    const int L = ZL_LANG_OBERON;
    const int U = ZL_E_UNSUPPORTED, S = ZL_E_SYNTAX;
    expect_refused(L, "MODULE t;\nIMPORT In;\nBEGIN\nEND t.\n", U, 2, "other modules");
    expect_refused(L, "MODULE t;\nIMPORT O := Out;\nBEGIN\nEND t.\n", U, 2, "import aliases");
    expect_refused(L, OD("VAR x*: INTEGER;", ""), U, 3, "export marks");
    expect_refused(L, OD("VAR c: CHAR;", ""), U, 3, "CHAR");
    expect_refused(L, OD("VAR r: REAL;", ""), U, 3, "REAL");
    expect_refused(L, OD("VAR b: BYTE;", ""), U, 3, "BYTE");
    expect_refused(L, OD("VAR s: SET;", ""), U, 3, "SET");
    expect_refused(L, OD("TYPE T = INTEGER;", ""), U, 3, "TYPE");
    expect_refused(L, OD("VAR r: RECORD a: INTEGER END;", ""), U, 3, "RECORD");
    expect_refused(L, OD("VAR p: POINTER TO RECORD END;", ""), U, 3, "POINTER");
    expect_refused(L, OD("VAR a: ARRAY 2, 2 OF INTEGER;", ""), U, 3, "2-D arrays");
    expect_refused(L, OD("PROCEDURE P(a: ARRAY OF INTEGER);\nBEGIN END P;", ""), U, 3,
                   "open array parameters");
    expect_refused(L, OD("VAR p: PROCEDURE;", ""), U, 3, "procedure variables");
    expect_refused(L, OD("CONST k = 1 + 2;", ""), U, 3, "constant expressions");
    expect_refused(L, OS("Out.Int(0FFH, 0)"), U, 4, "hexadecimal literals");
    expect_refused(L, OS("Out.Char(41X)"), U, 4, "character codes");
    expect_refused(L, OS("Out.Int(1.5, 0)"), U, 4, "real literals");
    expect_refused(L, OS("Out.Int(4 / 2, 0)"), U, 4, "real division");
    expect_refused(L, OD("VAR my_x: INTEGER;", ""), U, 3, "underscores in names");
    expect_refused(L, OS("Out.Int(ORD(TRUE), 0)"), U, 4, "ORD");
    expect_refused(L, OS("Out.Int(LSL(1, 2), 0)"), U, 4, "LSL");
    expect_refused(L, OS("ASSERT(TRUE)"), U, 4, "ASSERT");
    expect_refused(L, OS("Out.Int(LEN(x), 0)"), U, 4, "LEN");
    expect_refused(L, OS("Out.Real(1, 0)"), U, 4, "other Out procedures");
    expect_refused(L, OS("IF 1 IN {1} THEN END"), U, 4, "IN and sets");
    expect_refused(L, OD("VAR i: INTEGER;", "CASE i OF 1: ELSE END"), U, 5, "CASE ... ELSE");
    expect_refused(L, OD("VAR x: INTEGER;", "x := 1;\nOut.Int(1, x)"), U, 6, "a variable width");
    /* syntax, scope and type errors */
    expect_refused(L, OD("PROCEDURE P;\nVAR a: INTEGER;\n  PROCEDURE Q;\n  BEGIN a := 1 END Q;\n"
                         "BEGIN END P;",
                         ""),
                   ZL_E_UNDEFINED, 6, "a variable of the procedure around");
    expect_refused(L, OD("PROCEDURE P;\nBEGIN END P;\nVAR x: INTEGER;", ""), S, 5,
                   "a VAR after the procedures");
    expect_refused(L, OD("PROCEDURE F(): INTEGER;\nBEGIN END F;", ""), S, 4,
                   "a function without its RETURN");
    expect_refused(L, OD("PROCEDURE F(): INTEGER;\nBEGIN IF TRUE THEN RETURN 1 END\nRETURN 2 END F;", ""), S,
                   4, "RETURN inside a statement");
    expect_refused(L, OD("PROCEDURE P;\nBEGIN END Q;", ""), S, 4, "END with the wrong name");
    expect_refused(L, OD("PROCEDURE P;\nBEGIN\nRETURN 1 END P;", ""), S, 5,
                   "RETURN with a value in a proper procedure");
    expect_refused(L, OS("LOOP END"), ZL_E_UNDEFINED, 4, "LOOP is not Oberon-07");
    expect_refused(L, OS("IF 1 <> 2 THEN END"), S, 4, "<> is not Oberon");
    expect_refused(L, OD("VAR i: INTEGER;", "FOR i := 1 TO 2 BY 0 DO END"), S, 5, "BY 0");
    expect_refused(L, OS("CASE 1 OF 1: | 1: END"), S, 4, "a label used twice");
    expect_refused(L, OS("CASE 1 OF -1: END"), S, 4, "a signed CASE label");
    expect_refused(L, OS("CASE 1 OF 1..+2: END"), S, 4, "a signed CASE range bound");
    expect_refused(L, OD("CONST m = -1;", "CASE 1 OF m: END"), U, 5, "a negative CASE label (OBNC refuses one)");
    expect_refused(L, OS("Out.Int(TRUE, 0)"), S, 4, "Out.Int of a BOOLEAN");
    expect_refused(L, "module t;\nbegin\nend t.\n", S, 1, "keywords are upper case");
    expect_refused(L, OD("VAR x: INTEGER;", "X := 1"), ZL_E_UNDEFINED, 5, "names are case-sensitive");
    expect_refused(L, OS("Out.String(\"a\""), S, 5, "an unclosed call");
}

static void test_oberon_traps(void)
{
    const int L = ZL_LANG_OBERON;
    expect_trap(L, OD("VAR a: ARRAY 3 OF INTEGER; i: INTEGER;", "i := 3;\na[i] := 1"), ZL_E_BOUNDS, 6,
                "an index past the end");
    expect_trap(L, OD("VAR a: ARRAY 3 OF INTEGER; i: INTEGER;", "i := -1;\nOut.Int(a[i], 0)"),
                ZL_E_BOUNDS, 6, "a negative index");
    expect_trap(L, OD("VAR i: INTEGER;", "i := 9;\nCASE i OF 1: Out.String(\"a\") | 2: END"), ZL_E_DOMAIN,
                6, "CASE with no matching label");
    expect_trap(L,
                OD("PROCEDURE F(n: INTEGER): INTEGER;\nRETURN F(n + 1)\nEND F;", "Out.Int(F(0), 0)"),
                ZL_E_RSTACK, 0, "endless recursion");
    expect_trap(L, OD("VAR i: INTEGER;", "WHILE TRUE DO INC(i, 0) END"), ZL_E_FUEL, 0, "an endless WHILE");
}

static void test_oberon(void)
{
    test_oberon_output();
    test_oberon_arith();
    test_oberon_statements();
    test_oberon_procs();
    test_oberon_twin();
    test_oberon_refusals();
    test_oberon_traps();
}
