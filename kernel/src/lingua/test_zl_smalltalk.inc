/* test_zl_smalltalk.inc — Smalltalk front-end tests, included by test_zl.c. */

static void test_smalltalk(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SMALLTALK, "", &d) == ZL_E_UNSUPPORTED, "Smalltalk is not built yet");
}
