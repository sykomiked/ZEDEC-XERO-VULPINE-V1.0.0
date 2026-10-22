/* test_zl_snobol.inc — SNOBOL front-end tests, included by test_zl.c. */

static void test_snobol(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SNOBOL, "", &d) == ZL_E_UNSUPPORTED, "SNOBOL is not built yet");
}
