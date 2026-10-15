/* test_zl_erlang.inc — Erlang front-end tests, included by test_zl.c. */

static void test_erlang(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ERLANG, "", &d) == ZL_E_UNSUPPORTED, "Erlang is not built yet");
}
