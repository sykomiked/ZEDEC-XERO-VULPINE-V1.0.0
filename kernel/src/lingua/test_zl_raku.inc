/* test_zl_raku.inc — Raku front-end tests, included by test_zl.c. */

static void test_raku(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_RAKU, "", &d) == ZL_E_UNSUPPORTED, "Raku is not built yet");
}
