/* test_zl_b.inc — B front-end tests, included by test_zl.c. */

static void test_b(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_B, "", &d) == ZL_E_UNSUPPORTED, "B is not built yet");
}
