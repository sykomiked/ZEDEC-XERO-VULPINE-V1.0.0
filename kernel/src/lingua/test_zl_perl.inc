/* test_zl_perl.inc — Perl front-end tests, included by test_zl.c. */

static void test_perl(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_PERL, "", &d) == ZL_E_UNSUPPORTED, "Perl is not built yet");
}
