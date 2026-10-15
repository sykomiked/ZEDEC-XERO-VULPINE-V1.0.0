/* test_zl_scala.inc — Scala front-end tests, included by test_zl.c. */

static void test_scala(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SCALA, "", &d) == ZL_E_UNSUPPORTED, "Scala is not built yet");
}
