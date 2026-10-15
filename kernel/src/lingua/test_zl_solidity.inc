/* test_zl_solidity.inc — Solidity front-end tests, included by test_zl.c. */

static void test_solidity(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SOLIDITY, "", &d) == ZL_E_UNSUPPORTED, "Solidity is not built yet");
}
