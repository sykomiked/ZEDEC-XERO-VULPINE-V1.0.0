/* test_zl_julia.inc — Julia front-end tests, included by test_zl.c. */

static void test_julia(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_JULIA, "", &d) == ZL_E_UNSUPPORTED, "Julia is not built yet");
}
