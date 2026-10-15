/* test_zl_ocaml.inc — OCaml front-end tests, included by test_zl.c. */

static void test_ocaml(void)
{
    zl_diag_t d;
    CHECK(compile(ZL_LANG_OCAML, "", &d) == ZL_E_UNSUPPORTED, "OCaml is not built yet");
}
