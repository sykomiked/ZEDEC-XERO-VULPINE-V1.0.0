/* test_zl_iecst.inc — IEC 61131-3 Structured Text front-end tests, included by test_zl.c. */

static void test_iecst(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_IEC_ST, "", &d) == ZL_E_UNSUPPORTED, "IEC 61131-3 Structured Text is not built yet");
}
