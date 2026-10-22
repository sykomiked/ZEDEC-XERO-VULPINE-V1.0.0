/* test_zl_apt.inc — APT front-end tests, included by test_zl.c. */

static void test_apt(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_APT, "", &d) == ZL_E_UNSUPPORTED, "APT is not built yet");
}
