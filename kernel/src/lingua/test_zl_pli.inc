/* test_zl_pli.inc — PL/I front-end tests, included by test_zl.c. */

static void test_pli(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_PLI, "", &d) == ZL_E_UNSUPPORTED, "PL/I is not built yet");
}
