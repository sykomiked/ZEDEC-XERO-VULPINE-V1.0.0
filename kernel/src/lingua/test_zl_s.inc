/* test_zl_s.inc — S front-end tests, included by test_zl.c. */

static void test_s(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_S, "", &d) == ZL_E_UNSUPPORTED, "S is not built yet");
}
