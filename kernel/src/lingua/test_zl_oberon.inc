/* test_zl_oberon.inc — Oberon front-end tests, included by test_zl.c. */

static void test_oberon(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_OBERON, "", &d) == ZL_E_UNSUPPORTED, "Oberon is not built yet");
}
