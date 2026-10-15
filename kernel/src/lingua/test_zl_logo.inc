/* test_zl_logo.inc — logo front-end tests, included by test_zl.c. */

static void test_logo(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_LOGO, "", &d) == ZL_E_UNSUPPORTED, "logo is not built yet");
}
