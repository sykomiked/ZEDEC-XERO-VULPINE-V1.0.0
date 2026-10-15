/* test_zl_verilog.inc — Verilog front-end tests, included by test_zl.c. */

static void test_verilog(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_VERILOG, "", &d) == ZL_E_UNSUPPORTED, "Verilog is not built yet");
}
