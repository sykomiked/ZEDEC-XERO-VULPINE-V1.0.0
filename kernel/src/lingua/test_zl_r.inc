/* test_zl_r.inc — R front-end tests, included by test_zl.c. */

static void test_r(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_R, "", &d) == ZL_E_UNSUPPORTED, "R is not built yet");
}
