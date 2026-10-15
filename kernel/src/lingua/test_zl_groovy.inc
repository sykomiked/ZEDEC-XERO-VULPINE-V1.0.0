/* test_zl_groovy.inc — Groovy front-end tests, included by test_zl.c. */

static void test_groovy(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_GROOVY, "", &d) == ZL_E_UNSUPPORTED, "Groovy is not built yet");
}
