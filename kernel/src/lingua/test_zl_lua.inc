/* test_zl_lua.inc — Lua front-end tests, included by test_zl.c. */

static void test_lua(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_LUA, "", &d) == ZL_E_UNSUPPORTED, "Lua is not built yet");
}
