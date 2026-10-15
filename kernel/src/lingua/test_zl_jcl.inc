/* test_zl_jcl.inc — JCL front-end tests, included by test_zl.c. */

static void test_jcl(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_JCL, "", &d) == ZL_E_UNSUPPORTED, "JCL is not built yet");
}
