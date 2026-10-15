/* test_zl_shortcode.inc — Short Code front-end tests, included by test_zl.c. */

static void test_shortcode(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SHORTCODE, "", &d) == ZL_E_UNSUPPORTED, "Short Code is not built yet");
}
