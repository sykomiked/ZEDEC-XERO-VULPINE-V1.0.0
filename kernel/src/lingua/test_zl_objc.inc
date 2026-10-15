/* test_zl_objc.inc — Objective-C front-end tests, included by test_zl.c. */

static void test_objc(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_OBJC, "", &d) == ZL_E_UNSUPPORTED, "Objective-C is not built yet");
}
