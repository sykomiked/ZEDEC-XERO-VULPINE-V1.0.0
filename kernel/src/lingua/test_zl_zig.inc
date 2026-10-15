/* test_zl_zig.inc — Zig front-end tests, included by test_zl.c. */

static void test_zig(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ZIG, "", &d) == ZL_E_UNSUPPORTED, "Zig is not built yet");
}
