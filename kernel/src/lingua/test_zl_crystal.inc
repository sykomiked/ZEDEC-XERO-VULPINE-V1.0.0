/* test_zl_crystal.inc — Crystal front-end tests, included by test_zl.c. */

static void test_crystal(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_CRYSTAL, "", &d) == ZL_E_UNSUPPORTED, "Crystal is not built yet");
}
