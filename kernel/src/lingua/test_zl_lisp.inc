/* test_zl_lisp.inc — lisp front-end tests, included by test_zl.c. */

static void test_lisp(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_LISP, "", &d) == ZL_E_UNSUPPORTED, "lisp is not built yet");
}
