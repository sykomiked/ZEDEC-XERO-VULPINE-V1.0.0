/* test_zl_trac.inc — TRAC front-end tests, included by test_zl.c. */

static void test_trac(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_TRAC, "", &d) == ZL_E_UNSUPPORTED, "TRAC is not built yet");
}
