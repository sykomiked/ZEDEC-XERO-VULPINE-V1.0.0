/* test_zl_pascal.inc — pascal front-end tests, included by test_zl.c. */

static void test_pascal(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_PASCAL, "", &d) == ZL_E_UNSUPPORTED, "pascal is not built yet");
}
