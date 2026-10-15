/* test_zl_delphi.inc — Delphi front-end tests, included by test_zl.c. */

static void test_delphi(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_DELPHI, "", &d) == ZL_E_UNSUPPORTED, "Delphi is not built yet");
}
