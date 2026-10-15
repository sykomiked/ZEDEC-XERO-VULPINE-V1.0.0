/* test_zl_bash.inc — Bash front-end tests, included by test_zl.c. */

static void test_bash(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_BASH, "", &d) == ZL_E_UNSUPPORTED, "Bash is not built yet");
}
