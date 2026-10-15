/* test_zl_modula2.inc — Modula-2 front-end tests, included by test_zl.c. */

static void test_modula2(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_MODULA2, "", &d) == ZL_E_UNSUPPORTED, "Modula-2 is not built yet");
}
