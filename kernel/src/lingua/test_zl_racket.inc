/* test_zl_racket.inc — Racket front-end tests, included by test_zl.c. */

static void test_racket(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_RACKET, "", &d) == ZL_E_UNSUPPORTED, "Racket is not built yet");
}
