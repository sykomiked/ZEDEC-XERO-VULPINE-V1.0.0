/* test_zl_flowmatic.inc — FLOW-MATIC front-end tests, included by test_zl.c. */

static void test_flowmatic(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_FLOWMATIC, "", &d) == ZL_E_UNSUPPORTED, "FLOW-MATIC is not built yet");
}
