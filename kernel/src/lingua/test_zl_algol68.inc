/* test_zl_algol68.inc — ALGOL 68 front-end tests, included by test_zl.c. */

static void test_algol68(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ALGOL68, "", &d) == ZL_E_UNSUPPORTED, "ALGOL 68 is not built yet");
}
