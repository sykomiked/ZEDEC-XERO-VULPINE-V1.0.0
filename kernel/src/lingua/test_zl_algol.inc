/* test_zl_algol.inc — algol front-end tests, included by test_zl.c. */

static void test_algol(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_ALGOL60, "", &d) == ZL_E_UNSUPPORTED, "algol is not built yet");
}
