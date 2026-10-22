/* test_zl_simula1.inc — Simula I front-end tests, included by test_zl.c. */

static void test_simula1(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SIMULA1, "", &d) == ZL_E_UNSUPPORTED, "Simula I is not built yet");
}
