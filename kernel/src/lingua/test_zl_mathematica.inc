/* test_zl_mathematica.inc — Mathematica front-end tests, included by test_zl.c. */

static void test_mathematica(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_MATHEMATICA, "", &d) == ZL_E_UNSUPPORTED, "Mathematica is not built yet");
}
