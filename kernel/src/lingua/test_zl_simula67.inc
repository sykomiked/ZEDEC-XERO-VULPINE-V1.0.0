/* test_zl_simula67.inc — Simula 67 front-end tests, included by test_zl.c. */

static void test_simula67(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SIMULA67, "", &d) == ZL_E_UNSUPPORTED, "Simula 67 is not built yet");
}
