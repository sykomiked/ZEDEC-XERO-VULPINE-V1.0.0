/* test_zl_matlab.inc — MATLAB front-end tests, included by test_zl.c. */

static void test_matlab(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_MATLAB, "", &d) == ZL_E_UNSUPPORTED, "MATLAB is not built yet");
}
