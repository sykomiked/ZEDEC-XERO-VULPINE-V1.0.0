/* test_zl_joss.inc — JOSS front-end tests, included by test_zl.c. */

static void test_joss(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_JOSS, "", &d) == ZL_E_UNSUPPORTED, "JOSS is not built yet");
}
