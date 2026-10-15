/* test_zl_elm.inc — Elm front-end tests, included by test_zl.c. */

static void test_elm(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ELM, "", &d) == ZL_E_UNSUPPORTED, "Elm is not built yet");
}
