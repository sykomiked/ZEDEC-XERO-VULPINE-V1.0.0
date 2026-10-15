/* test_zl_dbase.inc — dBase front-end tests, included by test_zl.c. */

static void test_dbase(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_DBASE, "", &d) == ZL_E_UNSUPPORTED, "dBase is not built yet");
}
