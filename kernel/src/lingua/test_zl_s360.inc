/* test_zl_s360.inc — System/360 assembler front-end tests, included by test_zl.c. */

static void test_s360(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_S360, "", &d) == ZL_E_UNSUPPORTED, "System/360 assembler is not built yet");
}
