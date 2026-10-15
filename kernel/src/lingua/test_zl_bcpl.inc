/* test_zl_bcpl.inc — BCPL front-end tests, included by test_zl.c. */

static void test_bcpl(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_BCPL, "", &d) == ZL_E_UNSUPPORTED, "BCPL is not built yet");
}
