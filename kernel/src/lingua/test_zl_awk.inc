/* test_zl_awk.inc — AWK (Fielddrake) front-end tests, included by
 * test_zl.c. The integer subset of POSIX awk: BEGIN/END blocks, integer
 * variables, arithmetic, control flow, print/printf. Expected outputs are
 * what awk (One True Awk) prints for the same programs; '/' and '^' are
 * refused because they float, and the comma of print joins with a tab. */

static void test_awk(void)
{
    expect_out(ZL_LANG_AWK, "BEGIN { print 1 }\n", "1\n", "print");
    expect_out(ZL_LANG_AWK, "BEGIN { } END { }\n", "", "empty blocks");
    expect_out(ZL_LANG_AWK, "BEGIN { x = 3\n y = 4\n print x * y }\n", "12\n", "variables");
    expect_out(ZL_LANG_AWK, "BEGIN { x = 5\n x += 2\n x--\n print x }\n", "6\n", "compound assign, --");
    expect_out(ZL_LANG_AWK, "BEGIN { print 17 % 5 \" \" -4 }\n", "2 -4\n", "mod, unary minus, adjacency");
    expect_out(ZL_LANG_AWK, "BEGIN { print !0 \" \" !5 }\n", "1 0\n", "logical not");
    expect_out(ZL_LANG_AWK, "BEGIN { print (3 < 4) \" \" (3 > 4) \" \" (4 == 4) }\n", "1 0 1\n",
               "parenthesised comparisons");
    expect_out(ZL_LANG_AWK, "BEGIN { print (1 && 0) \" \" (0 || 7) }\n", "0 1\n", "&& ||");
    expect_out(ZL_LANG_AWK, "BEGIN { print 1, 2 }\n", "1\t2\n", "print comma is a tab");
    expect_out(ZL_LANG_AWK, "BEGIN { x = 1 ? 10 : 20\n print x }\n", "10\n", "ternary");
    expect_out(ZL_LANG_AWK, "BEGIN { if (3 > 2) { print \"yes\" } else { print \"no\" } }\n", "yes\n",
               "if else");
    expect_out(ZL_LANG_AWK, "BEGIN { i = 0\n while (i < 5) { i += 2 }\n print i }\n", "6\n", "while");
    expect_out(ZL_LANG_AWK, "BEGIN { for (i = 0; i < 3; i++) { print i } }\n", "0\n1\n2\n", "for");
    expect_out(ZL_LANG_AWK,
               "BEGIN { for (i = 0; i < 10; i++) { if (i == 4) { break }\n if (i % 2) { continue }\n"
               " print i } }\n",
               "0\n2\n", "break and continue");
    expect_out(ZL_LANG_AWK, "BEGIN { print \"a\"\n exit\n print \"never\" }\n", "a\n", "exit");
    expect_out(ZL_LANG_AWK, "BEGIN { x = 7\n print int(x) }\n", "7\n", "int()");
    expect_out(ZL_LANG_AWK, "BEGIN { printf \"%d|%s|%%\\n\", 7, \"x\" }\n", "7|x|%\n", "printf");
    expect_out(ZL_LANG_AWK, "BEGIN { x = 1 } END { print x + 1 }\n", "2\n", "BEGIN then END");
    expect_out(ZL_LANG_AWK, "# just a comment\nBEGIN { print \"ok\" }\n", "ok\n", "comment");

    /* refusals by name: the record model, floats, arrays and friends are
     * outside the subset and must be told so, not guessed at. */
    expect_refused(ZL_LANG_AWK, "BEGIN { x = 4 / 2 }\n", ZL_E_UNSUPPORTED, 1, "'/' floats");
    expect_refused(ZL_LANG_AWK, "BEGIN { x = 2 ^ 3 }\n", ZL_E_UNSUPPORTED, 1, "'^' floats");
    expect_refused(ZL_LANG_AWK, "BEGIN { print $1 }\n", ZL_E_UNSUPPORTED, 1, "the record model");
    expect_refused(ZL_LANG_AWK, "NR > 2 { print }\n", ZL_E_UNSUPPORTED, 1, "a bare rule");
    expect_refused(ZL_LANG_AWK, "BEGIN { a[1] = 2 }\n", ZL_E_UNSUPPORTED, 1, "arrays");
    expect_refused(ZL_LANG_AWK, "BEGIN { for (k in a) { print k } }\n", ZL_E_UNSUPPORTED, 1,
                   "for (i in a)");
    expect_refused(ZL_LANG_AWK, "BEGIN { print 1.5 }\n", ZL_E_UNSUPPORTED, 1, "a float literal");
    expect_refused(ZL_LANG_AWK, "BEGIN { print length(\"x\") }\n", ZL_E_UNSUPPORTED, 1,
                   "the string functions");
    expect_refused(ZL_LANG_AWK, "BEGIN { print 1 < 2 }\n", ZL_E_SYNTAX, 1,
                   "a bare comparison in print");
}
