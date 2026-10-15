/* test_zl_lz.inc — Laning and Zierler front-end tests, included by test_zl.c. */

static void test_lz(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_LZ, "", &d) == ZL_E_UNSUPPORTED, "Laning and Zierler is not built yet");
}
