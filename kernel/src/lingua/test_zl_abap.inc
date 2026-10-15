/* test_zl_abap.inc — ABAP front-end tests, included by test_zl.c. */

static void test_abap(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ABAP, "", &d) == ZL_E_UNSUPPORTED, "ABAP is not built yet");
}
