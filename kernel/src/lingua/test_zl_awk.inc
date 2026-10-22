/* test_zl_awk.inc — AWK front-end tests, included by test_zl.c. */

static void test_awk(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_AWK, "", &d) == ZL_E_UNSUPPORTED, "AWK is not built yet");
}
