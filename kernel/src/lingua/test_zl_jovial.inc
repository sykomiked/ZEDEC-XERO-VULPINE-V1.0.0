/* test_zl_jovial.inc — JOVIAL front-end tests, included by test_zl.c. */

static void test_jovial(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_JOVIAL, "", &d) == ZL_E_UNSUPPORTED, "JOVIAL is not built yet");
}
