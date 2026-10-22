/* test_zl_sas.inc — SAS front-end tests, included by test_zl.c. */

static void test_sas(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SAS, "", &d) == ZL_E_UNSUPPORTED, "SAS is not built yet");
}
