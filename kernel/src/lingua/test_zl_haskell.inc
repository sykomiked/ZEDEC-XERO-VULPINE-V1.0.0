/* test_zl_haskell.inc — Haskell front-end tests, included by test_zl.c. */

static void test_haskell(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_HASKELL, "", &d) == ZL_E_UNSUPPORTED, "Haskell is not built yet");
}
