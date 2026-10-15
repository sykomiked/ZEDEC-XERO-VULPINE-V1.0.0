/* test_zl_sql.inc — SQL front-end tests, included by test_zl.c. */

static void test_sql(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_SQL, "", &d) == ZL_E_UNSUPPORTED, "SQL is not built yet");
}
