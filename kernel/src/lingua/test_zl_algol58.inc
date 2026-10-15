/* test_zl_algol58.inc — ALGOL 58 front-end tests, included by test_zl.c. */

static void test_algol58(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ALGOL58, "", &d) == ZL_E_UNSUPPORTED, "ALGOL 58 is not built yet");
}
