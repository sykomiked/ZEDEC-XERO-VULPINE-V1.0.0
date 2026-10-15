/* test_zl_ipl.inc — IPL front-end tests, included by test_zl.c. */

static void test_ipl(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_IPL, "", &d) == ZL_E_UNSUPPORTED, "IPL is not built yet");
}
