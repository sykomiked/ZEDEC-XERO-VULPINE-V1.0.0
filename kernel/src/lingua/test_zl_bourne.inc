/* test_zl_bourne.inc — Bourne shell front-end tests, included by test_zl.c. */

static void test_bourne(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_BOURNE, "", &d) == ZL_E_UNSUPPORTED, "Bourne shell is not built yet");
}
