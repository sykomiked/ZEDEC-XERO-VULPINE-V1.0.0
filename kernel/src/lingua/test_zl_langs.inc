/* test_zl_langs.inc — the per-language test files, one per front-end, and
 * the function that runs them all. Included by test_zl.c. */
#include "test_zl_pascal.inc"
#include "test_zl_algol.inc"
#include "test_zl_lisp.inc"
#include "test_zl_logo.inc"
#include "test_zl_mojo.inc"
#include "test_zl_prolog.inc"
#include "test_zl_tex.inc"
#include "test_zl_haskell.inc"
#include "test_zl_ml.inc"
#include "test_zl_elixir.inc"
#include "test_zl_erlang.inc"
#include "test_zl_labview.inc"
#include "test_zl_eiffel.inc"
#include "test_zl_gleam.inc"
#include "test_zl_simula67.inc"
#include "test_zl_solidity.inc"
#include "test_zl_simula1.inc"
#include "test_zl_bash.inc"
#include "test_zl_bourne.inc"
#include "test_zl_jcl.inc"
#include "test_zl_julia.inc"
#include "test_zl_ocaml.inc"
#include "test_zl_occam.inc"
#include "test_zl_matlab.inc"
#include "test_zl_postscript.inc"
#include "test_zl_racket.inc"
#include "test_zl_raku.inc"
#include "test_zl_awk.inc"
#include "test_zl_icon.inc"
#include "test_zl_perl.inc"
#include "test_zl_snobol.inc"
#include "test_zl_trac.inc"
#include "test_zl_delphi.inc"
#include "test_zl_modula2.inc"
#include "test_zl_apt.inc"
#include "test_zl_oberon.inc"
#include "test_zl_clojure.inc"
#include "test_zl_iecst.inc"
#include "test_zl_jovial.inc"
#include "test_zl_ladder.inc"
#include "test_zl_ipl.inc"
#include "test_zl_rpg.inc"
#include "test_zl_sas.inc"
#include "test_zl_scala.inc"
#include "test_zl_b.inc"
#include "test_zl_bcpl.inc"
#include "test_zl_crystal.inc"
#include "test_zl_natural.inc"
#include "test_zl_nim.inc"
#include "test_zl_objc.inc"
#include "test_zl_sql.inc"
#include "test_zl_smalltalk.inc"
#include "test_zl_s360.inc"
#include "test_zl_zig.inc"
#include "test_zl_dbase.inc"
#include "test_zl_commonlisp.inc"
#include "test_zl_elm.inc"
#include "test_zl_groovy.inc"
#include "test_zl_hypertalk.inc"
#include "test_zl_lua.inc"
#include "test_zl_mathematica.inc"
#include "test_zl_rexx.inc"
#include "test_zl_tcl.inc"
#include "test_zl_algol58.inc"
#include "test_zl_algol68.inc"
#include "test_zl_autocode.inc"
#include "test_zl_cpl.inc"
#include "test_zl_joss.inc"
#include "test_zl_lz.inc"
#include "test_zl_abap.inc"
#include "test_zl_flowmatic.inc"
#include "test_zl_pli.inc"
#include "test_zl_r.inc"
#include "test_zl_s.inc"
#include "test_zl_shortcode.inc"
#include "test_zl_vhdl.inc"
#include "test_zl_verilog.inc"

static void test_langs(void)
{
    test_pascal();
    test_algol();
    test_lisp();
    test_logo();
    test_mojo();
    test_prolog();
    test_tex();
    test_haskell();
    test_ml();
    test_elixir();
    test_erlang();
    test_labview();
    test_eiffel();
    test_gleam();
    test_simula67();
    test_solidity();
    test_simula1();
    test_bash();
    test_bourne();
    test_jcl();
    test_julia();
    test_ocaml();
    test_occam();
    test_matlab();
    test_postscript();
    test_racket();
    test_raku();
    test_awk();
    test_icon();
    test_perl();
    test_snobol();
    test_trac();
    test_delphi();
    test_modula2();
    test_apt();
    test_oberon();
    test_clojure();
    test_iecst();
    test_jovial();
    test_ladder();
    test_ipl();
    test_rpg();
    test_sas();
    test_scala();
    test_b();
    test_bcpl();
    test_crystal();
    test_natural();
    test_nim();
    test_objc();
    test_sql();
    test_smalltalk();
    test_s360();
    test_zig();
    test_dbase();
    test_commonlisp();
    test_elm();
    test_groovy();
    test_hypertalk();
    test_lua();
    test_mathematica();
    test_rexx();
    test_tcl();
    test_algol58();
    test_algol68();
    test_autocode();
    test_cpl();
    test_joss();
    test_lz();
    test_abap();
    test_flowmatic();
    test_pli();
    test_r();
    test_s();
    test_shortcode();
    test_vhdl();
    test_verilog();
}
