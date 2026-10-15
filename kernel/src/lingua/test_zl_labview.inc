/* test_zl_labview.inc — LabVIEW front-end tests, included by test_zl.c. */

static void test_labview(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_LABVIEW, "", &d) == ZL_E_UNSUPPORTED, "LabVIEW is not built yet");
}
