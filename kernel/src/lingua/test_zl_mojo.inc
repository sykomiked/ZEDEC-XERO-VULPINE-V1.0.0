/* test_zl_mojo.inc — Mojo front-end tests, included by test_zl.c. */

static void test_mojo(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_MOJO, "", &d) == ZL_E_UNSUPPORTED, "Mojo is not built yet");
}
