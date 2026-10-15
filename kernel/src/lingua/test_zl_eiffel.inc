/* test_zl_eiffel.inc — Eiffel front-end tests, included by test_zl.c. */

static void test_eiffel(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_EIFFEL, "", &d) == ZL_E_UNSUPPORTED, "Eiffel is not built yet");
}
