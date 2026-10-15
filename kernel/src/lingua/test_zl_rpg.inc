/* test_zl_rpg.inc — RPG front-end tests, included by test_zl.c. */

static void test_rpg(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_RPG, "", &d) == ZL_E_UNSUPPORTED, "RPG is not built yet");
}
