/* test_zl_tex.inc — TeX front-end tests, included by test_zl.c. */

static void test_tex(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_TEX, "", &d) == ZL_E_UNSUPPORTED, "TeX is not built yet");
}
