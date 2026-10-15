/* test_zl_prolog.inc — Prolog front-end tests, included by test_zl.c. */

static void test_prolog(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_PROLOG, "", &d) == ZL_E_UNSUPPORTED, "Prolog is not built yet");
}
