/* test_zl_vhdl.inc — VHDL front-end tests, included by test_zl.c. */

static void test_vhdl(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_VHDL, "", &d) == ZL_E_UNSUPPORTED, "VHDL is not built yet");
}
