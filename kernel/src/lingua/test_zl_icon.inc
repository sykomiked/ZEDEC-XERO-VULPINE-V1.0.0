/* test_zl_icon.inc — Icon front-end tests, included by test_zl.c. */

static void test_icon(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ICON, "", &d) == ZL_E_UNSUPPORTED, "Icon is not built yet");
}
