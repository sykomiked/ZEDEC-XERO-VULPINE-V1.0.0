/* test_zl_elixir.inc — Elixir front-end tests, included by test_zl.c. */

static void test_elixir(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ELIXIR, "", &d) == ZL_E_UNSUPPORTED, "Elixir is not built yet");
}
