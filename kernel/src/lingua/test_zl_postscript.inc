/* test_zl_postscript.inc — PostScript front-end tests, included by test_zl.c. */

static void test_postscript(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_POSTSCRIPT, "", &d) == ZL_E_UNSUPPORTED, "PostScript is not built yet");
}
