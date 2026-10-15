/* test_zl_commonlisp.inc — Common Lisp front-end tests, included by test_zl.c. */

static void test_commonlisp(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_COMMONLISP, "", &d) == ZL_E_UNSUPPORTED, "Common Lisp is not built yet");
}
