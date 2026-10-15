/* test_zl_hypertalk.inc — HyperTalk front-end tests, included by test_zl.c. */

static void test_hypertalk(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_HYPERTALK, "", &d) == ZL_E_UNSUPPORTED, "HyperTalk is not built yet");
}
