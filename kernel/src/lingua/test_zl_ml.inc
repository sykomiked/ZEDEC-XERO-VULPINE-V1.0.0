/* test_zl_ml.inc — ML front-end tests, included by test_zl.c. */

static void test_ml(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ML, "", &d) == ZL_E_UNSUPPORTED, "ML is not built yet");
}
