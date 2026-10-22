/* test_zl_ladder.inc — Ladder logic front-end tests, included by test_zl.c. */

static void test_ladder(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_LADDER, "", &d) == ZL_E_UNSUPPORTED, "Ladder logic is not built yet");
}
