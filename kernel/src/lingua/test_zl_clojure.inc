/* test_zl_clojure.inc — Clojure front-end tests, included by test_zl.c. */

static void test_clojure(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_CLOJURE, "", &d) == ZL_E_UNSUPPORTED, "Clojure is not built yet");
}
