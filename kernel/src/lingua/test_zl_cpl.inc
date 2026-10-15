/* test_zl_cpl.inc — CPL front-end tests, included by test_zl.c. */

static void test_cpl(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_CPL, "", &d) == ZL_E_UNSUPPORTED, "CPL is not built yet");
}
