/* test_zl_gleam.inc — Gleam front-end tests, included by test_zl.c. */

static void test_gleam(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_GLEAM, "", &d) == ZL_E_UNSUPPORTED, "Gleam is not built yet");
}
