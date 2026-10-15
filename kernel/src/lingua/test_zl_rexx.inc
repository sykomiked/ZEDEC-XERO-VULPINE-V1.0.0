/* test_zl_rexx.inc — REXX front-end tests, included by test_zl.c. */

static void test_rexx(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_REXX, "", &d) == ZL_E_UNSUPPORTED, "REXX is not built yet");
}
