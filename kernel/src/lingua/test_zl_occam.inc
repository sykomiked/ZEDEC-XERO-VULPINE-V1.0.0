/* test_zl_occam.inc — Occam front-end tests, included by test_zl.c. */

static void test_occam(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_OCCAM, "", &d) == ZL_E_UNSUPPORTED, "Occam is not built yet");
}
