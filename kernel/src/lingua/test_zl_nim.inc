/* test_zl_nim.inc — Nim front-end tests, included by test_zl.c. */

static void test_nim(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_NIM, "", &d) == ZL_E_UNSUPPORTED, "Nim is not built yet");
}
