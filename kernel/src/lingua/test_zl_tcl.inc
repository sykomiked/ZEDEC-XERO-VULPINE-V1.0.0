/* test_zl_tcl.inc — Tcl front-end tests, included by test_zl.c. */

static void test_tcl(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_TCL, "", &d) == ZL_E_UNSUPPORTED, "Tcl is not built yet");
}
