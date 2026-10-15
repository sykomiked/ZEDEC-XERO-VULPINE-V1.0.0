/* test_zl_autocode.inc — Autocode front-end tests, included by test_zl.c. */

static void test_autocode(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_AUTOCODE, "", &d) == ZL_E_UNSUPPORTED, "Autocode is not built yet");
}
