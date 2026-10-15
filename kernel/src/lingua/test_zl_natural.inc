/* test_zl_natural.inc — Natural front-end tests, included by test_zl.c. */

static void test_natural(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_NATURAL, "", &d) == ZL_E_UNSUPPORTED, "Natural is not built yet");
}
