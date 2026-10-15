/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_ml.c — the ML family front-end: Standard ML '97 as MLton runs it (twin
 * "Proofdrake") and OCaml as the ocaml toplevel runs it (twin
 * "Variantwyrm"), one parser with two surface syntaxes, one Hindley-Milner
 * type checker and one code generator. c->p->lang says which.
 *
 * Values are integers, booleans (0 and 1), unit (0) and lists (pairs; the
 * empty list is ZL_V_NIL). Functions are first order: every function is
 * named and every call names it with all its arguments. Static types are
 * inferred over int, bool, unit and lists (let-polymorphism for functions
 * and value bindings that are values), so an ill-typed program is refused
 * as the reference refuses it.
 *
 * Admitted in both: integer literals, true and false, (), lists ([] and
 * literals, ::, @), the arithmetic + - * and the comparisons (= and <> are
 * structural on lists), if/then/else, case/match on patterns (wildcards,
 * variables, integers, booleans, (), [], ::, list patterns, tuples of
 * patterns), let ... in, sequencing, named functions with curried and
 * tupled parameters ("fun f (a, b) x = ..."), defined by clauses,
 * recursive (and mutually recursive with "and") and nested, with
 * access to the variables of the functions around them; tuples only where
 * they are taken apart at once (a call's argument, a case scrutinee, the
 * right side of a tuple binding); not, abs, hd, tl, length, rev and the
 * printing functions. A self call in tail position runs as a loop.
 * Evaluation order is the reference's: SML evaluates left to right; OCaml
 * evaluates operands, arguments, tuples and list elements right to left
 * (as ocaml does), but the components of a tuple matched directly by
 * "match" left to right.
 *
 * ML (SML '97, MLton): val, fun with clauses, and, let/in/end, case/of,
 * andalso/orelse, ~ (also in literals: ~5), div and mod (floored), Int.quot
 * and Int.rem (truncating), Int.min, Int.max, Int.abs, List.hd ...,
 * null, print with string expressions built from literals, Int.toString
 * (which writes ~ for a minus sign), Bool.toString and ^; a top-level
 * expression "e;". Integers are MLton's default Int (32 bits): a result
 * outside traps OVERFLOW where MLton raises Overflow; Div traps DIVZERO.
 * A program that uses lists runs with 62-bit words and checks the 32-bit
 * range after every operation.
 *
 * OCaml (4.14): let, let rec ... and, let ... in, match ... with (also
 * with "when" guards), function as the right side of a function binding,
 * if without else (unit), begin/end, && and ||, / and mod (truncating),
 * unary minus, min and max (on integers and booleans), succ, pred, max_int,
 * min_int, List.hd, List.tl, List.length, List.rev, print_int,
 * print_string, print_endline, print_newline, string_of_int,
 * string_of_bool, and Printf.printf with %d %i %b %s and %%; top-level
 * expressions after ";;". Integers are 63-bit and wrap exactly as OCaml's
 * do (a product is computed modulo 2^63 by a helper routine). A program
 * that uses lists runs with 62-bit words: there an integer result outside
 * (-2^62, 2^62) traps OVERFLOW instead of wrapping, and min_int is refused.
 *
 * Refused by name (ZL_E_UNSUPPORTED): functions as values (fn, fun, lambda,
 * partial application, a function name not called), tuples as values,
 * strings and characters as values (a string may only be printed), reals
 * and floats, hexadecimal and other literal forms, references and
 * assignment, exceptions (raise, handle, try, exception), datatype, type,
 * records, modules and structures, open, local, while and for loops,
 * or-patterns, as-patterns, type annotations, polymorphic comparison of
 * lists (<, >, ... on lists), OCaml's physical equality == and !=, and
 * library functions outside the list above. A non-exhaustive match that
 * fails traps DOMAIN (Match/Bind), as do hd and tl of the empty list.
 * Non-tail recursion deeper than the call stack (ZL_MAX_CALLS frames, or
 * ZL_MAX_STACK pending operands) traps where the reference would go on.
 *
 * The twins: one top-level declaration or comment per line, a comment kept
 * only between top-level declarations, written "(* text *)"; inside a
 * declaration comments are dropped. One space between tokens, none inside
 * parentheses and brackets or before a comma; parentheses only where the
 * grammar needs them, and around an open form (if, case/match, OCaml let
 * and sequences) that is not the last thing in its context; SML clauses
 * after the first on their own line as "  | f p = e"; tuples always in
 * parentheses. A top-level expression is written "val _ = e" (SML) or
 * "let () = e" / "let _ = e" (OCaml), begin/end as parentheses, and SML
 * semicolons between declarations and OCaml ";;" are dropped. */
#include "zl_ml_int.h"

/* ------------------------------------------------------------ tokens */

enum {
    T_EOF = 0,
    T_INT,
    T_ID,
    T_LONG,
    T_STR,
    T_LP,
    T_RP,
    T_LB,
    T_RB,
    T_COMMA,
    T_SEMI,
    T_SEMI2,
    T_BAR,
    T_ARROW, /* => in SML, -> in OCaml */
    T_EQ,
    T_NE,
    T_LT,
    T_LE,
    T_GT,
    T_GE,
    T_PLUS,
    T_MINUS,
    T_STAR,
    T_SLASH,
    T_CARET,
    T_CONS,
    T_AT,
    T_TILDE,
    T_WILD,
    T_AND2,
    T_OR2,
    K_VAL,
    K_FUN,
    K_AND,
    K_LET,
    K_IN,
    K_END,
    K_IF,
    K_THEN,
    K_ELSE,
    K_CASE,
    K_OF,
    K_ANDALSO,
    K_ORELSE,
    K_DIV,
    K_MOD,
    K_TRUE,
    K_FALSE,
    K_REC,
    K_MATCH,
    K_WITH,
    K_WHEN,
    K_BEGIN,
    K_FUNCTION
};

typedef struct {
    const char *s;
    uint8_t dial; /* 1 SML, 2 OCaml, 3 both */
    uint8_t tok;  /* 0: refused with msg */
    const char *msg;
} mkw_t;

static const mkw_t kws[] = {
    {"val", 1, K_VAL, 0},
    {"fun", 1, K_FUN, 0},
    {"and", 3, K_AND, 0},
    {"let", 3, K_LET, 0},
    {"in", 3, K_IN, 0},
    {"end", 3, K_END, 0},
    {"if", 3, K_IF, 0},
    {"then", 3, K_THEN, 0},
    {"else", 3, K_ELSE, 0},
    {"case", 1, K_CASE, 0},
    {"of", 1, K_OF, 0},
    {"andalso", 1, K_ANDALSO, 0},
    {"orelse", 1, K_ORELSE, 0},
    {"div", 1, K_DIV, 0},
    {"mod", 3, K_MOD, 0},
    {"true", 3, K_TRUE, 0},
    {"false", 3, K_FALSE, 0},
    {"rec", 2, K_REC, 0},
    {"match", 2, K_MATCH, 0},
    {"with", 2, K_WITH, 0},
    {"when", 2, K_WHEN, 0},
    {"begin", 2, K_BEGIN, 0},
    {"function", 2, K_FUNCTION, 0},
    {"fn", 1, 0, "fn (functions as values) is not in this subset"},
    {"fun", 2, 0, "fun (functions as values) is not in this subset"},
    {"rec", 1, 0, "val rec is not in this subset (use fun)"},
    {"raise", 1, 0, "exceptions (raise) are not in this subset"},
    {"handle", 1, 0, "exceptions (handle) are not in this subset"},
    {"exception", 3, 0, "exceptions are not in this subset"},
    {"try", 2, 0, "exceptions (try) are not in this subset"},
    {"datatype", 1, 0, "datatype is not in this subset"},
    {"abstype", 1, 0, "abstype is not in this subset"},
    {"type", 3, 0, "type declarations are not in this subset"},
    {"eqtype", 1, 0, "eqtype is not in this subset"},
    {"withtype", 1, 0, "withtype is not in this subset"},
    {"structure", 1, 0, "structures are not in this subset"},
    {"signature", 1, 0, "signatures are not in this subset"},
    {"functor", 3, 0, "functors are not in this subset"},
    {"struct", 3, 0, "structures are not in this subset"},
    {"sig", 3, 0, "signatures are not in this subset"},
    {"sharing", 1, 0, "signatures are not in this subset"},
    {"where", 1, 0, "signatures are not in this subset"},
    {"include", 3, 0, "include is not in this subset"},
    {"open", 3, 0, "open is not in this subset"},
    {"local", 1, 0, "local is not in this subset"},
    {"infix", 1, 0, "fixity declarations are not in this subset"},
    {"infixr", 1, 0, "fixity declarations are not in this subset"},
    {"nonfix", 1, 0, "fixity declarations are not in this subset"},
    {"op", 1, 0, "op (operators as values) is not in this subset"},
    {"as", 3, 0, "as-patterns are not in this subset"},
    {"while", 3, 0, "while loops are not in this subset"},
    {"do", 3, 0, "loops are not in this subset"},
    {"for", 2, 0, "for loops are not in this subset"},
    {"done", 2, 0, "loops are not in this subset"},
    {"to", 2, 0, "for loops are not in this subset"},
    {"downto", 2, 0, "for loops are not in this subset"},
    {"with", 1, 0, "with is not in this subset"},
    {"module", 2, 0, "modules are not in this subset"},
    {"val", 2, 0, "signatures are not in this subset"},
    {"of", 2, 0, "variant types are not in this subset"},
    {"assert", 2, 0, "assert is not in this subset"},
    {"lazy", 2, 0, "lazy values are not in this subset"},
    {"mutable", 2, 0, "mutable fields are not in this subset"},
    {"external", 2, 0, "external is not in this subset"},
    {"class", 2, 0, "objects are not in this subset"},
    {"object", 2, 0, "objects are not in this subset"},
    {"method", 2, 0, "objects are not in this subset"},
    {"new", 2, 0, "objects are not in this subset"},
    {"inherit", 2, 0, "objects are not in this subset"},
    {"initializer", 2, 0, "objects are not in this subset"},
    {"virtual", 2, 0, "objects are not in this subset"},
    {"private", 2, 0, "objects are not in this subset"},
    {"constraint", 2, 0, "type constraints are not in this subset"},
    {"nonrec", 2, 0, "nonrec is not in this subset"},
    {"or", 2, 0, "or is not in this subset (use ||)"},
    {"land", 2, 0, "bitwise operators are not in this subset"},
    {"lor", 2, 0, "bitwise operators are not in this subset"},
    {"lxor", 2, 0, "bitwise operators are not in this subset"},
    {"lsl", 2, 0, "bitwise operators are not in this subset"},
    {"lsr", 2, 0, "bitwise operators are not in this subset"},
    {"asr", 2, 0, "bitwise operators are not in this subset"},
};

typedef struct {
    const char *s;
    uint8_t dial, id, ng, ar; /* ng curried groups (0: a value), ar: arity of each */
    const char *msg;          /* refused */
} mbi_t;

static const mbi_t bis[] = {
    {"print", 1, B_PRINT, 1, 1, 0},
    {"Int.toString", 1, B_ITOS, 1, 1, 0},
    {"Bool.toString", 1, B_BTOS, 1, 1, 0},
    {"abs", 3, B_ABS, 1, 1, 0},
    {"Int.abs", 1, B_ABS, 1, 1, 0},
    {"not", 3, B_NOT, 1, 1, 0},
    {"Int.min", 1, B_MIN, 1, 2, 0},
    {"Int.max", 1, B_MAX, 1, 2, 0},
    {"Int.quot", 1, B_QUOT, 1, 2, 0},
    {"Int.rem", 1, B_REM, 1, 2, 0},
    {"min", 2, B_MIN, 2, 1, 0},
    {"max", 2, B_MAX, 2, 1, 0},
    {"hd", 1, B_HD, 1, 1, 0},
    {"tl", 1, B_TL, 1, 1, 0},
    {"null", 1, B_NULL, 1, 1, 0},
    {"length", 1, B_LENGTH, 1, 1, 0},
    {"rev", 1, B_REV, 1, 1, 0},
    {"List.hd", 3, B_HD, 1, 1, 0},
    {"List.tl", 3, B_TL, 1, 1, 0},
    {"List.null", 1, B_NULL, 1, 1, 0},
    {"List.length", 3, B_LENGTH, 1, 1, 0},
    {"List.rev", 3, B_REV, 1, 1, 0},
    {"print_int", 2, B_PRINT_INT, 1, 1, 0},
    {"print_string", 2, B_PRINT_STRING, 1, 1, 0},
    {"print_newline", 2, B_PRINT_NEWLINE, 1, 1, 0},
    {"print_endline", 2, B_PRINT_ENDLINE, 1, 1, 0},
    {"Printf.printf", 2, B_PRINTF, 1, 1, 0},
    {"string_of_int", 2, B_ITOS, 1, 1, 0},
    {"string_of_bool", 2, B_BTOS, 1, 1, 0},
    {"succ", 2, B_SUCC, 1, 1, 0},
    {"pred", 2, B_PRED, 1, 1, 0},
    {"max_int", 2, B_MAXINT, 0, 0, 0},
    {"min_int", 2, B_MININT, 0, 0, 0},
    {"ref", 3, 0, 0, 0, "references are not in this subset"},
    {"raise", 2, 0, 0, 0, "exceptions (raise) are not in this subset"},
    {"failwith", 2, 0, 0, 0, "exceptions (failwith) are not in this subset"},
    {"invalid_arg", 2, 0, 0, 0, "exceptions (invalid_arg) are not in this subset"},
    {"fst", 2, 0, 0, 0, "tuples as values (fst) are not in this subset"},
    {"snd", 2, 0, 0, 0, "tuples as values (snd) are not in this subset"},
    {"ignore", 3, 0, 0, 0, "ignore is not in this subset"},
    {"map", 1, 0, 0, 0, "higher-order functions (map) are not in this subset"},
    {"foldl", 1, 0, 0, 0, "higher-order functions (foldl) are not in this subset"},
    {"foldr", 1, 0, 0, 0, "higher-order functions (foldr) are not in this subset"},
    {"app", 1, 0, 0, 0, "higher-order functions (app) are not in this subset"},
    {"o", 1, 0, 0, 0, "function composition is not in this subset"},
    {"real", 1, 0, 0, 0, "reals are not in this subset"},
    {"float_of_int", 2, 0, 0, 0, "floats are not in this subset"},
    {"float", 2, 0, 0, 0, "floats are not in this subset"},
    {"compare", 2, 0, 0, 0, "compare is not in this subset"},
    {"valOf", 1, 0, 0, 0, "options are not in this subset"},
    {"SOME", 1, 0, 0, 0, "options are not in this subset"},
    {"NONE", 1, 0, 0, 0, "options are not in this subset"},
    {"read_int", 2, 0, 0, 0, "input is not in this subset"},
    {"read_line", 2, 0, 0, 0, "input is not in this subset"},
    {"exit", 2, 0, 0, 0, "exit is not in this subset"},
    /* Haskell (the Prelude) */
    {"print", 4, B_HPRINT, 1, 1, 0},
    {"putStrLn", 4, B_PRINT_ENDLINE, 1, 1, 0},
    {"putStr", 4, B_PRINT_STRING, 1, 1, 0},
    {"show", 4, B_SHOW, 1, 1, 0},
    {"return", 4, B_RETURN, 1, 1, 0},
    {"pure", 4, B_RETURN, 1, 1, 0},
    {"abs", 4, B_ABS, 1, 1, 0},
    {"signum", 4, B_SIGNUM, 1, 1, 0},
    {"negate", 4, B_NEGATE, 1, 1, 0},
    {"not", 4, B_NOT, 1, 1, 0},
    {"even", 4, B_EVEN, 1, 1, 0},
    {"odd", 4, B_ODD, 1, 1, 0},
    {"succ", 4, B_SUCC, 1, 1, 0},
    {"pred", 4, B_PRED, 1, 1, 0},
    {"min", 4, B_MIN, 2, 1, 0},
    {"max", 4, B_MAX, 2, 1, 0},
    {"gcd", 4, B_GCD, 2, 1, 0},
    {"lcm", 4, B_LCM, 2, 1, 0},
    {"div", 4, B_DIVOP, 2, 1, 0},
    {"mod", 4, B_DIVOP, 2, 1, 0},
    {"quot", 4, B_DIVOP, 2, 1, 0},
    {"rem", 4, B_DIVOP, 2, 1, 0},
    {"head", 4, B_HD, 1, 1, 0},
    {"tail", 4, B_TL, 1, 1, 0},
    {"null", 4, B_NULL, 1, 1, 0},
    {"length", 4, B_LENGTH, 1, 1, 0},
    {"reverse", 4, B_REV, 1, 1, 0},
    {"sum", 4, B_SUM, 1, 1, 0},
    {"product", 4, B_PRODUCT, 1, 1, 0},
    {"maximum", 4, B_MAXIMUM, 1, 1, 0},
    {"minimum", 4, B_MINIMUM, 1, 1, 0},
    {"take", 4, B_TAKE, 2, 1, 0},
    {"drop", 4, B_DROP, 2, 1, 0},
    {"elem", 4, B_ELEM, 2, 1, 0},
    {"notElem", 4, B_NOTELEM, 2, 1, 0},
    {"replicate", 4, B_REPLICATE, 2, 1, 0},
    {"last", 4, B_LAST, 1, 1, 0},
    {"init", 4, B_INIT, 1, 1, 0},
    {"concat", 4, B_CONCAT, 1, 1, 0},
    {"and", 4, B_AND, 1, 1, 0},
    {"or", 4, B_OR, 1, 1, 0},
    {"otherwise", 4, B_OTHERWISE, 0, 0, 0},
    {"main", 4, 0, 0, 0, "using main inside the program is not in this subset"},
    {"map", 4, 0, 0, 0, "higher-order functions (map) are not in this subset"},
    {"filter", 4, 0, 0, 0, "higher-order functions (filter) are not in this subset"},
    {"foldr", 4, 0, 0, 0, "higher-order functions (foldr) are not in this subset"},
    {"foldl", 4, 0, 0, 0, "higher-order functions (foldl) are not in this subset"},
    {"foldr1", 4, 0, 0, 0, "higher-order functions (foldr1) are not in this subset"},
    {"foldl1", 4, 0, 0, 0, "higher-order functions (foldl1) are not in this subset"},
    {"scanl", 4, 0, 0, 0, "higher-order functions (scanl) are not in this subset"},
    {"scanr", 4, 0, 0, 0, "higher-order functions (scanr) are not in this subset"},
    {"zipWith", 4, 0, 0, 0, "higher-order functions (zipWith) are not in this subset"},
    {"zipWith3", 4, 0, 0, 0, "higher-order functions (zipWith3) are not in this subset"},
    {"any", 4, 0, 0, 0, "higher-order functions (any) are not in this subset"},
    {"all", 4, 0, 0, 0, "higher-order functions (all) are not in this subset"},
    {"concatMap", 4, 0, 0, 0, "higher-order functions (concatMap) are not in this subset"},
    {"takeWhile", 4, 0, 0, 0, "higher-order functions (takeWhile) are not in this subset"},
    {"dropWhile", 4, 0, 0, 0, "higher-order functions (dropWhile) are not in this subset"},
    {"span", 4, 0, 0, 0, "higher-order functions (span) are not in this subset"},
    {"break", 4, 0, 0, 0, "higher-order functions (break) are not in this subset"},
    {"until", 4, 0, 0, 0, "higher-order functions (until) are not in this subset"},
    {"iterate", 4, 0, 0, 0, "infinite lists (iterate) are not in this subset"},
    {"repeat", 4, 0, 0, 0, "infinite lists (repeat) are not in this subset"},
    {"cycle", 4, 0, 0, 0, "infinite lists (cycle) are not in this subset"},
    {"id", 4, 0, 0, 0, "functions as values (id) are not in this subset"},
    {"const", 4, 0, 0, 0, "functions as values (const) are not in this subset"},
    {"flip", 4, 0, 0, 0, "functions as values (flip) are not in this subset"},
    {"curry", 4, 0, 0, 0, "functions as values (curry) are not in this subset"},
    {"uncurry", 4, 0, 0, 0, "functions as values (uncurry) are not in this subset"},
    {"subtract", 4, 0, 0, 0, "subtract is not in this subset (use -)"},
    {"seq", 4, 0, 0, 0, "seq is not in this subset"},
    {"zip", 4, 0, 0, 0, "tuples as values (zip) are not in this subset"},
    {"zip3", 4, 0, 0, 0, "tuples as values (zip3) are not in this subset"},
    {"unzip", 4, 0, 0, 0, "tuples as values (unzip) are not in this subset"},
    {"fst", 4, 0, 0, 0, "tuples as values (fst) are not in this subset"},
    {"snd", 4, 0, 0, 0, "tuples as values (snd) are not in this subset"},
    {"lookup", 4, 0, 0, 0, "tuples as values (lookup) are not in this subset"},
    {"splitAt", 4, 0, 0, 0, "tuples as values (splitAt) are not in this subset"},
    {"divMod", 4, 0, 0, 0, "tuples as values (divMod) are not in this subset"},
    {"quotRem", 4, 0, 0, 0, "tuples as values (quotRem) are not in this subset"},
    {"error", 4, 0, 0, 0, "error is not in this subset"},
    {"errorWithoutStackTrace", 4, 0, 0, 0, "error is not in this subset"},
    {"undefined", 4, 0, 0, 0, "undefined is not in this subset"},
    {"fromIntegral", 4, 0, 0, 0, "numeric conversions (fromIntegral) are not in this subset"},
    {"fromInteger", 4, 0, 0, 0, "numeric conversions (fromInteger) are not in this subset"},
    {"toInteger", 4, 0, 0, 0, "Integer is not in this subset"},
    {"toEnum", 4, 0, 0, 0, "Enum conversions are not in this subset"},
    {"fromEnum", 4, 0, 0, 0, "Enum conversions are not in this subset"},
    {"maxBound", 4, 0, 0, 0, "maxBound (a class method) is not in this subset"},
    {"minBound", 4, 0, 0, 0, "minBound (a class method) is not in this subset"},
    {"compare", 4, 0, 0, 0, "compare (Ordering) is not in this subset"},
    {"words", 4, 0, 0, 0, "strings as values (words) are not in this subset"},
    {"unwords", 4, 0, 0, 0, "strings as values (unwords) are not in this subset"},
    {"lines", 4, 0, 0, 0, "strings as values (lines) are not in this subset"},
    {"unlines", 4, 0, 0, 0, "strings as values (unlines) are not in this subset"},
    {"shows", 4, 0, 0, 0, "strings as values (shows) are not in this subset"},
    {"read", 4, 0, 0, 0, "read is not in this subset"},
    {"getLine", 4, 0, 0, 0, "input is not in this subset"},
    {"getContents", 4, 0, 0, 0, "input is not in this subset"},
    {"interact", 4, 0, 0, 0, "input is not in this subset"},
    {"readLn", 4, 0, 0, 0, "input is not in this subset"},
    {"putChar", 4, 0, 0, 0, "characters are not in this subset"},
    {"mapM_", 4, 0, 0, 0, "higher-order functions (mapM_) are not in this subset"},
    {"mapM", 4, 0, 0, 0, "higher-order functions (mapM) are not in this subset"},
    {"forM_", 4, 0, 0, 0, "higher-order functions (forM_) are not in this subset"},
    {"forM", 4, 0, 0, 0, "higher-order functions (forM) are not in this subset"},
    {"sequence", 4, 0, 0, 0, "sequence is not in this subset"},
    {"sequence_", 4, 0, 0, 0, "sequence_ is not in this subset"},
    {"when", 4, 0, 0, 0, "when (Control.Monad) is not in this subset"},
    {"unless", 4, 0, 0, 0, "unless (Control.Monad) is not in this subset"},
    {"sqrt", 4, 0, 0, 0, "floating point (sqrt) is not in this subset"},
    {"pi", 4, 0, 0, 0, "floating point (pi) is not in this subset"},
    {"truncate", 4, 0, 0, 0, "floating point (truncate) is not in this subset"},
    {"round", 4, 0, 0, 0, "floating point (round) is not in this subset"},
    {"floor", 4, 0, 0, 0, "floating point (floor) is not in this subset"},
    {"ceiling", 4, 0, 0, 0, "floating point (ceiling) is not in this subset"},
    {"realToFrac", 4, 0, 0, 0, "floating point (realToFrac) is not in this subset"},
    {"exp", 4, 0, 0, 0, "floating point (exp) is not in this subset"},
    {"log", 4, 0, 0, 0, "floating point (log) is not in this subset"},
    {"fromRational", 4, 0, 0, 0, "fractions are not in this subset"},
    {"toRational", 4, 0, 0, 0, "fractions are not in this subset"},
    {"recip", 4, 0, 0, 0, "fractions are not in this subset"},
    {"mempty", 4, 0, 0, 0, "monoids are not in this subset"},
    {"mconcat", 4, 0, 0, 0, "monoids are not in this subset"},
    {"fmap", 4, 0, 0, 0, "functors are not in this subset"},
    {"maybe", 4, 0, 0, 0, "Maybe is not in this subset"},
    {"either", 4, 0, 0, 0, "Either is not in this subset"},
    {"ioError", 4, 0, 0, 0, "exceptions are not in this subset"},
    {"userError", 4, 0, 0, 0, "exceptions are not in this subset"},
    {"asTypeOf", 4, 0, 0, 0, "asTypeOf is not in this subset"},
};


static int mfail(mw_t *w, int st, uint32_t line, uint32_t col, const char *msg)
{
    if (!w->c->failed) {
        w->c->line = line;
        w->c->col = col;
    }
    return zl_fail(w->c, st, msg);
}

static int tfail(mw_t *w, int st, const char *msg)
{
    return mfail(w, st, w->tok.line, w->tok.col, msg);
}

static int nfail(mw_t *w, int32_t n, int st, const char *msg)
{
    return mfail(w, st, N(w, n)->line, N(w, n)->col, msg);
}

/* ------------------------------------------------------------ lexer */

static int is_sym(int dial, int ch)
{
    if (dial == D_SML) {
        switch (ch) {
        case '!': case '%': case '&': case '$': case '#': case '+': case '-': case '/':
        case ':': case '<': case '=': case '>': case '?': case '@': case '\\': case '~':
        case '`': case '^': case '|': case '*':
            return 1;
        default:
            return 0;
        }
    }
    switch (ch) {
    case '!': case '$': case '%': case '&': case '*': case '+': case '-': case '.': case '/':
    case ':': case '<': case '=': case '>': case '?': case '@': case '^': case '|': case '~':
    case '#':
        return 1;
    default:
        return 0;
    }
}

static int is_idch(int ch)
{
    return zl_isalnum(ch) || ch == '_' || ch == '\'';
}

static int spell(const char *a, size_t n, const char *s)
{
    size_t i = 0;
    for (; i < n; i++)
        if (s[i] == 0 || s[i] != a[i]) return 0;
    return s[i] == 0;
}

/* Skip blanks and comments (nested); comments are recorded. */
static int skip(mw_t *w)
{
    mlex_t *l = &w->lx;
    for (;;) {
        if (l->pos >= w->len) return ZL_OK;
        char ch = w->src[l->pos];
        if (ch == '\n') {
            l->pos++;
            l->line++;
            l->lstart = l->pos;
        } else if (ch == ' ' || ch == '\t' || ch == '\r') {
            l->pos++;
        } else if (ch == '(' && l->pos + 1 < w->len && w->src[l->pos + 1] == '*') {
            uint32_t line = l->line, col = (uint32_t) (l->pos - l->lstart + 1);
            size_t a = l->pos + 2;
            int depth = 1;
            l->pos += 2;
            while (depth) {
                if (l->pos >= w->len) return mfail(w, ZL_E_SYNTAX, line, col, "unterminated comment");
                char d = w->src[l->pos];
                if (d == '\n') {
                    l->pos++;
                    l->line++;
                    l->lstart = l->pos;
                } else if (d == '(' && l->pos + 1 < w->len && w->src[l->pos + 1] == '*') {
                    depth++;
                    l->pos += 2;
                } else if (d == '*' && l->pos + 1 < w->len && w->src[l->pos + 1] == ')') {
                    depth--;
                    l->pos += 2;
                } else if (d == '"' && w->dial == D_OCAML) {
                    return mfail(w, ZL_E_UNSUPPORTED, l->line, (uint32_t) (l->pos - l->lstart + 1),
                                 "a string inside a comment is not in this subset");
                } else if ((unsigned char) d < 32 && d != '\t' && d != '\r') {
                    return mfail(w, ZL_E_UNSUPPORTED, l->line, (uint32_t) (l->pos - l->lstart + 1),
                                 "a control character in a comment");
                } else {
                    l->pos++;
                }
            }
            size_t b = l->pos - 2;
            while (a < b && (zl_isspace((unsigned char) w->src[a]) || w->src[a] == '\n')) a++;
            while (b > a && (zl_isspace((unsigned char) w->src[b - 1]) || w->src[b - 1] == '\n')) b--;
            if (b - a > ZL_MAX_STRBYTES) return mfail(w, ZL_E_LIMIT, line, col, "comment too long");
            if (w->ncom >= M_COMS) return mfail(w, ZL_E_LIMIT, line, col, "too many comments");
            w->coms[w->ncom].at = a;
            w->coms[w->ncom].len = (uint32_t) (b - a);
            w->coms[w->ncom].line = line;
            w->ncom++;
        } else {
            return ZL_OK;
        }
    }
}

static int lex(mw_t *w)
{
    mlex_t *l = &w->lx;
    mtok_t *t = &w->tok;
    w->last_end = t->at + t->len;
    TRY(skip(w));
    t->at = l->pos;
    t->line = l->line;
    t->col = (uint32_t) (l->pos - l->lstart + 1);
    t->len = 0;
    t->v = 0;
    t->big = 0;
    if (l->pos >= w->len) {
        t->k = T_EOF;
        return ZL_OK;
    }
    const char *s = w->src;
    size_t p = l->pos, n = w->len;
    unsigned char ch = (unsigned char) s[p];
    int neg = 0;
    if (w->dial == D_SML && ch == '~' && p + 1 < n && zl_isdigit((unsigned char) s[p + 1])) {
        neg = 1;
        p++;
        ch = (unsigned char) s[p];
    }
    if (zl_isdigit(ch)) {
        uint64_t v = 0;
        int big = 0;
        if (ch == '0' && p + 1 < n && (s[p + 1] == 'x' || s[p + 1] == 'X'))
            return tfail(w, ZL_E_UNSUPPORTED, "hexadecimal literals are not in this subset");
        if (ch == '0' && p + 1 < n && (s[p + 1] == 'w' || s[p + 1] == 'o' || s[p + 1] == 'O' ||
                                       s[p + 1] == 'b' || s[p + 1] == 'B'))
            return tfail(w, ZL_E_UNSUPPORTED, "word, octal and binary literals are not in this subset");
        while (p < n && zl_isdigit((unsigned char) s[p])) {
            v = v * 10 + (uint64_t) (s[p] - '0');
            if (v > ((uint64_t) 1 << 62)) {
                big = 1;
                v = (uint64_t) 1 << 62;
            }
            p++;
        }
        if (p < n && (s[p] == '.' || s[p] == 'e' || s[p] == 'E'))
            return tfail(w, ZL_E_UNSUPPORTED, "real and float numbers are not in this subset");
        if (p < n && (s[p] == '_' || s[p] == 'l' || s[p] == 'L' || s[p] == 'n'))
            return tfail(w, ZL_E_UNSUPPORTED, "this integer literal form is not in this subset");
        if (p < n && is_idch((unsigned char) s[p]))
            return tfail(w, ZL_E_SYNTAX, "a letter right after a number");
        t->k = T_INT;
        t->big = big;
        if (w->dial == D_SML) {
            int64_t sv = neg ? -(int64_t) v : (int64_t) v;
            if (big || sv > 2147483647 || sv < -2147483647 - 1)
                return tfail(w, ZL_E_SYNTAX, "integer constant too large for int");
            t->v = sv;
        } else {
            t->v = (int64_t) v;
        }
        t->len = (uint32_t) (p - l->pos);
        l->pos = p;
        return ZL_OK;
    }
    if (zl_isalpha(ch) || ch == '_') {
        size_t a = p;
        while (p < n && is_idch((unsigned char) s[p])) p++;
        if (ch == '_' && p == a + 1) {
            t->k = T_WILD;
        } else if (ch == '_' && w->dial == D_SML) {
            return tfail(w, ZL_E_SYNTAX, "an identifier cannot start with _");
        } else if (s[a] >= 'A' && s[a] <= 'Z' && p + 1 < n && s[p] == '.' &&
                   zl_isalpha((unsigned char) s[p + 1])) {
            p++;
            while (p < n && is_idch((unsigned char) s[p])) p++;
            if (p < n && s[p] == '.')
                return tfail(w, ZL_E_UNSUPPORTED, "nested module paths are not in this subset");
            t->k = T_LONG;
        } else {
            t->k = T_ID;
            for (size_t i = 0; i < sizeof kws / sizeof kws[0]; i++) {
                if (!(kws[i].dial & (1 << w->dial)) || !spell(s + a, p - a, kws[i].s)) continue;
                if (!kws[i].tok) return tfail(w, ZL_E_UNSUPPORTED, kws[i].msg);
                t->k = kws[i].tok;
                break;
            }
            if (t->k == T_ID && w->dial == D_OCAML && s[a] >= 'A' && s[a] <= 'Z')
                return tfail(w, ZL_E_UNSUPPORTED, "constructors and modules are not in this subset");
            if (t->k == T_ID && s[a] == '\'')
                return tfail(w, ZL_E_UNSUPPORTED, "type variables are not in this subset");
        }
        if (p - a > 64) return tfail(w, ZL_E_LIMIT, "name too long");
        t->len = (uint32_t) (p - a);
        l->pos = p;
        return ZL_OK;
    }
    if (ch == '"') {
        p++;
        while (p < n && s[p] != '"') {
            unsigned char d = (unsigned char) s[p];
            if (d == '\\') {
                if (p + 1 >= n) break;
                char e = s[p + 1];
                if (e != 'n' && e != 't' && e != '\\' && e != '"')
                    return mfail(w, ZL_E_UNSUPPORTED, l->line, (uint32_t) (p - l->lstart + 1),
                                 "only the escapes \\n \\t \\\\ \\\" are in this subset");
                p += 2;
                continue;
            }
            if (d < 32 || d > 126)
                return mfail(w, ZL_E_UNSUPPORTED, l->line, (uint32_t) (p - l->lstart + 1),
                             "only printable ASCII is in this subset's strings");
            p++;
        }
        if (p >= n) return tfail(w, ZL_E_SYNTAX, "unterminated string");
        p++;
        if (p - l->pos > M_STR) return tfail(w, ZL_E_LIMIT, "string too long");
        t->k = T_STR;
        t->len = (uint32_t) (p - l->pos);
        l->pos = p;
        return ZL_OK;
    }
    int k = 0;
    size_t one = 1;
    switch (ch) {
    case '(':
        k = T_LP;
        break;
    case ')':
        k = T_RP;
        break;
    case '[':
        if (w->dial == D_OCAML && p + 1 < n && s[p + 1] == '|')
            return tfail(w, ZL_E_UNSUPPORTED, "arrays are not in this subset");
        k = T_LB;
        break;
    case ']':
        k = T_RB;
        break;
    case ',':
        k = T_COMMA;
        break;
    case ';':
        if (w->dial == D_OCAML && p + 1 < n && s[p + 1] == ';') {
            k = T_SEMI2;
            one = 2;
        } else {
            k = T_SEMI;
        }
        break;
    case '{':
    case '}':
        return tfail(w, ZL_E_UNSUPPORTED, "records are not in this subset");
    case '\'':
        return tfail(w, ZL_E_UNSUPPORTED, "characters and type variables are not in this subset");
    case '#':
        if (w->dial == D_SML && p + 1 < n && s[p + 1] == '"')
            return tfail(w, ZL_E_UNSUPPORTED, "characters are not in this subset");
        if (w->dial == D_SML) return tfail(w, ZL_E_UNSUPPORTED, "record selectors are not in this subset");
        break;
    default:
        break;
    }
    if (k) {
        t->k = k;
        t->len = (uint32_t) one;
        l->pos = p + one;
        return ZL_OK;
    }
    if (is_sym(w->dial, ch)) {
        size_t a = p;
        while (p < n && is_sym(w->dial, (unsigned char) s[p])) p++;
        size_t m = p - a;
        const char *o = s + a;
        static const struct {
            const char *s;
            uint8_t dial, k;
            const char *msg;
        } ops[] = {
            {"=", 3, T_EQ, 0},
            {"<>", 3, T_NE, 0},
            {"<", 3, T_LT, 0},
            {"<=", 3, T_LE, 0},
            {">", 3, T_GT, 0},
            {">=", 3, T_GE, 0},
            {"+", 3, T_PLUS, 0},
            {"-", 3, T_MINUS, 0},
            {"*", 3, T_STAR, 0},
            {"^", 3, T_CARET, 0},
            {"::", 3, T_CONS, 0},
            {"@", 3, T_AT, 0},
            {"|", 3, T_BAR, 0},
            {"~", 1, T_TILDE, 0},
            {"=>", 1, T_ARROW, 0},
            {"->", 2, T_ARROW, 0},
            {"/", 2, T_SLASH, 0},
            {"&&", 2, T_AND2, 0},
            {"||", 2, T_OR2, 0},
            {"/", 1, 0, "real division (/) is not in this subset (use div)"},
            {":", 3, 0, "type annotations are not in this subset"},
            {":>", 1, 0, "signatures are not in this subset"},
            {"->", 1, 0, "function types are not in this subset"},
            {":=", 3, 0, "assignment (:=) is not in this subset"},
            {"!", 3, 0, "references (!) are not in this subset"},
            {"==", 2, 0, "physical equality (==) is not in this subset"},
            {"!=", 2, 0, "physical inequality (!=) is not in this subset"},
            {"**", 2, 0, "floating-point power (**) is not in this subset"},
            {"|>", 2, 0, "the |> operator (functions as values) is not in this subset"},
            {"@@", 2, 0, "the @@ operator (functions as values) is not in this subset"},
            {"~", 2, 0, "labelled arguments are not in this subset"},
            {"~-", 2, 0, "~- is not in this subset (use -)"},
            {"?", 2, 0, "optional arguments are not in this subset"},
            {".", 2, 0, "field access is not in this subset"},
            {"&", 2, 0, "& is not in this subset (use &&)"},
            {"<-", 2, 0, "assignment (<-) is not in this subset"},
        };
        for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
            if (!(ops[i].dial & (1 << w->dial)) || !spell(o, m, ops[i].s)) continue;
            if (!ops[i].k) return tfail(w, ZL_E_UNSUPPORTED, ops[i].msg);
            t->k = ops[i].k;
            t->len = (uint32_t) m;
            l->pos = p;
            return ZL_OK;
        }
        if (w->dial == D_OCAML && m >= 2 && o[m - 1] == '.')
            return tfail(w, ZL_E_UNSUPPORTED, "float operators are not in this subset");
        return tfail(w, ZL_E_UNSUPPORTED, "this operator is not in this subset");
    }
    return tfail(w, ZL_E_SYNTAX, "unexpected character");
}

/* ------------------------------------------------------------ parser */

static int32_t newnode(mw_t *w, int kind, const mtok_t *at)
{
    if (w->nn >= M_NODES)
        return mfail(w, ZL_E_LIMIT, at->line, at->col, "program too large for the ML front-end");
    int32_t i = (int32_t) w->nn++;
    mnode_t *n = N(w, i);
    zl_bzero(n, sizeof *n);
    n->kind = (uint8_t) kind;
    n->a = n->b = n->c = n->next = n->ty = n->sym = -1;
    n->line = at->line;
    n->col = (uint16_t) (at->col > 0xffff ? 0xffff : at->col);
    return i;
}

static int expect(mw_t *w, int k, const char *msg)
{
    if (w->tok.k != k) return tfail(w, ZL_E_SYNTAX, msg);
    return lex(w);
}

static int deeper(mw_t *w)
{
    if (++w->depth > M_DEPTH) return tfail(w, ZL_E_LIMIT, "expression nested too deeply");
    return ZL_OK;
}

static int32_t p_exp(mw_t *w);
static int32_t p_seq(mw_t *w);
static int32_t p_pat(mw_t *w);
static int32_t p_atpat(mw_t *w);
static int32_t p_decl(mw_t *w, int top);

static int32_t mklit(mw_t *w, int kind, int64_t v, const mtok_t *at)
{
    int32_t n = newnode(w, kind, at);
    if (n >= 0) N(w, n)->v = v;
    return n;
}

/* the value of an integer token, negated or not (OCaml: -2^62 needs the sign) */
static int intval(mw_t *w, const mtok_t *t, int neg, int64_t *v)
{
    if (w->dial == D_SML) {
        *v = neg ? -t->v : t->v;
        if (*v > 2147483647 || *v < -2147483647 - 1)
            return mfail(w, ZL_E_SYNTAX, t->line, t->col, "integer constant too large for int");
        return ZL_OK;
    }
    /* OCaml reads 2^62 as min_int, and -min_int is min_int again */
    int64_t lim = (int64_t) 1 << 62;
    if (t->big || t->v > lim)
        return mfail(w, ZL_E_SYNTAX, t->line, t->col,
                     "integer literal exceeds the range of representable integers of type int");
    *v = t->v == lim ? -lim : t->v;
    if (neg && *v != -lim) *v = -*v;
    return ZL_OK;
}

static int name_node(mw_t *w, int32_t n, const mtok_t *t)
{
    N(w, n)->v = (int64_t) t->at;
    N(w, n)->len = (uint16_t) t->len;
    return ZL_OK;
}

/* list of nodes: append */
typedef struct {
    int32_t head, tail;
} mlist_t;

static void ladd(mw_t *w, mlist_t *l, int32_t n)
{
    if (l->head < 0)
        l->head = n;
    else
        N(w, l->tail)->next = n;
    l->tail = n;
}

static int32_t p_atpat(mw_t *w)
{
    mtok_t t = w->tok;
    int32_t n;
    switch (t.k) {
    case T_WILD:
        n = newnode(w, P_WILD, &t);
        TRY(n);
        TRY(lex(w));
        return n;
    case T_ID:
        n = newnode(w, P_VAR, &t);
        TRY(n);
        name_node(w, n, &t);
        TRY(lex(w));
        return n;
    case T_INT: {
        int64_t v;
        TRY(intval(w, &t, 0, &v));
        n = mklit(w, P_INT, v, &t);
        TRY(n);
        TRY(lex(w));
        return n;
    }
    case T_MINUS:
        if (w->dial == D_OCAML) {
            TRY(lex(w));
            if (w->tok.k != T_INT) return tfail(w, ZL_E_SYNTAX, "a number after - in a pattern");
            int64_t v;
            TRY(intval(w, &w->tok, 1, &v));
            n = mklit(w, P_INT, v, &t);
            TRY(n);
            TRY(lex(w));
            return n;
        }
        break;
    case K_TRUE:
    case K_FALSE:
        n = mklit(w, P_BOOL, t.k == K_TRUE, &t);
        TRY(n);
        TRY(lex(w));
        return n;
    case T_STR:
        return tfail(w, ZL_E_UNSUPPORTED, "string patterns are not in this subset");
    case T_LP: {
        TRY(lex(w));
        if (w->tok.k == T_RP) {
            n = newnode(w, P_UNIT, &t);
            TRY(n);
            TRY(lex(w));
            return n;
        }
        int32_t p = p_pat(w);
        TRY(p);
        if (w->tok.k == T_COMMA) {
            n = newnode(w, P_TUPLE, &t);
            TRY(n);
            mlist_t l = {p, p};
            uint32_t k = 1;
            while (w->tok.k == T_COMMA) {
                TRY(lex(w));
                int32_t q = p_pat(w);
                TRY(q);
                ladd(w, &l, q);
                k++;
            }
            if (k > M_GROUPS) return tfail(w, ZL_E_LIMIT, "tuple too long");
            N(w, n)->a = l.head;
            N(w, n)->v = k;
            p = n;
        }
        TRY(expect(w, T_RP, "expected ) to close the pattern"));
        return p;
    }
    case T_LB: {
        w->lists = 1;
        TRY(lex(w));
        if (w->tok.k == T_RB) {
            n = newnode(w, P_NIL, &t);
            TRY(n);
            TRY(lex(w));
            return n;
        }
        n = newnode(w, P_LIST, &t);
        TRY(n);
        mlist_t l = {-1, -1};
        int sep = w->dial == D_SML ? T_COMMA : T_SEMI;
        uint32_t k = 0;
        for (;;) {
            int32_t q = p_pat(w);
            TRY(q);
            ladd(w, &l, q);
            k++;
            if (w->tok.k != sep) break;
            TRY(lex(w));
            if (w->dial == D_OCAML && w->tok.k == T_RB) break;
        }
        N(w, n)->a = l.head;
        N(w, n)->v = k;
        TRY(expect(w, T_RB, "expected ] to close the list pattern"));
        return n;
    }
    default:
        break;
    }
    return tfail(w, ZL_E_SYNTAX, "expected a pattern");
}

/* pat := atpat [:: pat]; OCaml also a, b at the top of a pattern */
static int32_t p_conspat(mw_t *w)
{
    TRY(deeper(w));
    int32_t a = p_atpat(w);
    TRY(a);
    if (w->tok.k == T_CONS) {
        mtok_t t = w->tok;
        w->lists = 1;
        TRY(lex(w));
        int32_t b = p_conspat(w);
        TRY(b);
        int32_t n = newnode(w, P_CONS, &t);
        TRY(n);
        N(w, n)->a = a;
        N(w, n)->b = b;
        a = n;
    }
    w->depth--;
    return a;
}

static int32_t p_pat(mw_t *w)
{
    int32_t p = p_conspat(w);
    TRY(p);
    if (w->tok.k == T_ID && spell(w->src + w->tok.at, w->tok.len, "as"))
        return tfail(w, ZL_E_UNSUPPORTED, "as-patterns are not in this subset");
    return p;
}

/* OCaml: a pattern that may be a tuple without parentheses */
static int32_t p_tpat(mw_t *w)
{
    mtok_t t = w->tok;
    int32_t p = p_pat(w);
    TRY(p);
    if (w->dial == D_OCAML && w->tok.k == T_COMMA) {
        int32_t n = newnode(w, P_TUPLE, &t);
        TRY(n);
        mlist_t l = {p, p};
        uint32_t k = 1;
        while (w->tok.k == T_COMMA) {
            TRY(lex(w));
            int32_t q = p_pat(w);
            TRY(q);
            ladd(w, &l, q);
            k++;
        }
        if (k > M_GROUPS) return tfail(w, ZL_E_LIMIT, "tuple too long");
        N(w, n)->a = l.head;
        N(w, n)->v = k;
        p = n;
    }
    if (w->tok.k == T_BAR && w->dial == D_OCAML)
        return tfail(w, ZL_E_UNSUPPORTED, "or-patterns are not in this subset");
    return p;
}

static int starts_atom(mw_t *w)
{
    switch (w->tok.k) {
    case T_INT:
    case T_ID:
    case T_LONG:
    case T_STR:
    case T_LP:
    case T_LB:
    case K_TRUE:
    case K_FALSE:
        return 1;
    case K_LET:
        return w->dial == D_SML;
    case K_BEGIN:
        return 1;
    default:
        return 0;
    }
}

/* match rules: pat => exp | ... (SML) or [|] pat [when e] -> e | ... (OCaml) */
static int32_t p_rules(mw_t *w)
{
    mlist_t l = {-1, -1};
    if (w->tok.k == T_BAR) {
        if (w->dial == D_SML) return tfail(w, ZL_E_SYNTAX, "a bar before the first rule");
        TRY(lex(w));
    }
    for (;;) {
        mtok_t t = w->tok;
        int32_t r = newnode(w, N_RULE, &t);
        TRY(r);
        int32_t p = w->dial == D_OCAML ? p_tpat(w) : p_pat(w);
        TRY(p);
        N(w, r)->a = p;
        if (w->dial == D_OCAML && w->tok.k == K_WHEN) {
            TRY(lex(w));
            int32_t g = p_seq(w);
            TRY(g);
            N(w, r)->b = g;
        }
        TRY(expect(w, T_ARROW, w->dial == D_SML ? "expected => in a rule" : "expected -> in a rule"));
        int32_t e = w->dial == D_SML ? p_exp(w) : p_seq(w);
        TRY(e);
        N(w, r)->c = e;
        ladd(w, &l, r);
        if (w->tok.k != T_BAR) break;
        TRY(lex(w));
    }
    return l.head;
}

static int32_t mkbin(mw_t *w, int op, int32_t a, int32_t b, const mtok_t *at)
{
    int32_t n = newnode(w, E_BIN, at);
    TRY(n);
    N(w, n)->op = (uint8_t) op;
    N(w, n)->a = a;
    N(w, n)->b = b;
    if (op == O_CONS || op == O_APPEND) w->lists = 1;
    return n;
}

/* atomic expressions */
static int32_t p_atom(mw_t *w)
{
    mtok_t t = w->tok;
    int32_t n;
    switch (t.k) {
    case T_INT: {
        int64_t v;
        TRY(intval(w, &t, 0, &v));
        n = mklit(w, E_INT, v, &t);
        TRY(n);
        TRY(lex(w));
        return n;
    }
    case K_TRUE:
    case K_FALSE:
        n = mklit(w, E_BOOL, t.k == K_TRUE, &t);
        TRY(n);
        TRY(lex(w));
        return n;
    case T_ID:
    case T_LONG:
        n = newnode(w, E_VAR, &t);
        TRY(n);
        name_node(w, n, &t);
        TRY(lex(w));
        return n;
    case T_STR:
        n = newnode(w, E_STR, &t);
        TRY(n);
        name_node(w, n, &t);
        TRY(lex(w));
        return n;
    case K_BEGIN:
    case T_LP: {
        int close = t.k == K_BEGIN ? K_END : T_RP;
        TRY(lex(w));
        if (w->tok.k == close) {
            n = newnode(w, E_UNIT, &t);
            TRY(n);
            TRY(lex(w));
            return n;
        }
        TRY(deeper(w));
        if (w->dial == D_OCAML) {
            n = p_seq(w);
            TRY(n);
            if (N(w, n)->kind == E_SEQ) N(w, n)->flags |= F_SEQPAREN;
        } else {
            int32_t e = p_exp(w);
            TRY(e);
            n = e;
            if (w->tok.k == T_COMMA || w->tok.k == T_SEMI) {
                int sep = w->tok.k;
                n = newnode(w, sep == T_COMMA ? E_TUPLE : E_SEQ, &t);
                TRY(n);
                mlist_t l = {e, e};
                uint32_t k = 1;
                while (w->tok.k == sep) {
                    TRY(lex(w));
                    int32_t q = p_exp(w);
                    TRY(q);
                    ladd(w, &l, q);
                    k++;
                }
                if (sep == T_COMMA && k > M_GROUPS) return tfail(w, ZL_E_LIMIT, "tuple too long");
                N(w, n)->a = l.head;
                N(w, n)->v = k;
            }
        }
        w->depth--;
        TRY(expect(w, close, close == K_END ? "expected end to close begin" : "expected )"));
        return n;
    }
    case T_LB: {
        w->lists = 1;
        TRY(lex(w));
        if (w->tok.k == T_RB) {
            n = newnode(w, E_NIL, &t);
            TRY(n);
            TRY(lex(w));
            return n;
        }
        TRY(deeper(w));
        n = newnode(w, E_LIST, &t);
        TRY(n);
        mlist_t l = {-1, -1};
        int sep = w->dial == D_SML ? T_COMMA : T_SEMI;
        uint32_t k = 0;
        for (;;) {
            int32_t e = p_exp(w);
            TRY(e);
            ladd(w, &l, e);
            k++;
            if (w->tok.k != sep) break;
            TRY(lex(w));
            if (w->dial == D_OCAML && w->tok.k == T_RB) break;
        }
        N(w, n)->a = l.head;
        N(w, n)->v = k;
        w->depth--;
        TRY(expect(w, T_RB, "expected ] to close the list"));
        return n;
    }
    case K_LET: { /* SML let decs in exp; ... end */
        TRY(lex(w));
        TRY(deeper(w));
        n = newnode(w, E_LET, &t);
        TRY(n);
        mlist_t l = {-1, -1};
        while (w->tok.k == K_VAL || w->tok.k == K_FUN || w->tok.k == T_SEMI) {
            if (w->tok.k == T_SEMI) {
                TRY(lex(w));
                continue;
            }
            int32_t d = p_decl(w, 0);
            TRY(d);
            ladd(w, &l, d);
        }
        if (l.head < 0) return tfail(w, ZL_E_SYNTAX, "expected a declaration after let");
        TRY(expect(w, K_IN, "expected in"));
        int32_t e = p_exp(w);
        TRY(e);
        if (w->tok.k == T_SEMI) {
            int32_t s = newnode(w, E_SEQ, &t);
            TRY(s);
            mlist_t q = {e, e};
            uint32_t k = 1;
            while (w->tok.k == T_SEMI) {
                TRY(lex(w));
                int32_t f = p_exp(w);
                TRY(f);
                ladd(w, &q, f);
                k++;
            }
            N(w, s)->a = q.head;
            N(w, s)->v = k;
            e = s;
        }
        TRY(expect(w, K_END, "expected end to close let"));
        N(w, n)->a = l.head;
        N(w, n)->b = e;
        w->depth--;
        return n;
    }
    default:
        break;
    }
    return tfail(w, ZL_E_SYNTAX, "expected an expression");
}

static int32_t fold_neg(mw_t *w, int32_t e, const mtok_t *t)
{
    int64_t v = N(w, e)->kind == E_INT ? N(w, e)->v : 0;
    if (N(w, e)->kind == E_INT && !(w->dial == D_SML && v == -2147483647 - 1)) {
        /* ~5 and -5 are literals; OCaml's -min_int wraps to min_int */
        N(w, e)->v = w->dial == D_OCAML && v == -((int64_t) 1 << 62) ? v : -v;
        N(w, e)->line = t->line;
        N(w, e)->col = (uint16_t) t->col;
        return e;
    }
    int32_t n = newnode(w, E_NEG, t);
    TRY(n);
    N(w, n)->a = e;
    return n;
}

static int is_open_start(mw_t *w)
{
    int k = w->tok.k;
    return k == K_IF || k == K_CASE || k == K_MATCH || (k == K_LET && w->dial == D_OCAML) ||
           k == K_FUNCTION;
}

/* application: f a b ...; SML ~ a */
static int32_t p_app(mw_t *w)
{
    mtok_t t = w->tok;
    if (w->dial == D_OCAML && is_open_start(w)) return p_exp(w);
    if (t.k == T_TILDE) {
        TRY(lex(w));
        if (!starts_atom(w)) return tfail(w, ZL_E_UNSUPPORTED, "~ as a value is not in this subset");
        int32_t a = p_atom(w);
        TRY(a);
        if (starts_atom(w)) return tfail(w, ZL_E_SYNTAX, "type error: the result of ~ is not a function");
        /* a negative literal written ~(5) or ~ 5 is the literal ~5 */
        return fold_neg(w, a, &t);
    }
    int32_t h = p_atom(w);
    TRY(h);
    if (!starts_atom(w) && !(w->dial == D_SML && w->tok.k == T_TILDE)) return h;
    if (N(w, h)->kind != E_VAR) {
        int k = N(w, h)->kind;
        if (k == E_INT || k == E_BOOL || k == E_UNIT || k == E_NIL || k == E_LIST || k == E_STR)
            return tfail(w, ZL_E_SYNTAX, "type error: a value that is not a function is applied");
        return tfail(w, ZL_E_UNSUPPORTED, "only a named function can be applied");
    }
    N(w, h)->kind = E_APP;
    mlist_t l = {-1, -1};
    uint32_t k = 0;
    while (starts_atom(w) || (w->dial == D_SML && w->tok.k == T_TILDE)) {
        if (w->tok.k == T_TILDE) return tfail(w, ZL_E_UNSUPPORTED, "~ as a value is not in this subset");
        int32_t a = p_atom(w);
        TRY(a);
        ladd(w, &l, a);
        if (++k > M_GROUPS) return tfail(w, ZL_E_LIMIT, "too many arguments");
    }
    N(w, h)->a = l.head;
    return h;
}

/* OCaml unary minus */
static int32_t p_neg(mw_t *w)
{
    if (w->dial == D_OCAML && w->tok.k == T_MINUS) {
        mtok_t t = w->tok;
        TRY(lex(w));
        TRY(deeper(w));
        int32_t e;
        if (w->tok.k == T_INT) { /* -n is a literal (and may be -2^62) */
            int64_t v;
            TRY(intval(w, &w->tok, 1, &v));
            mtok_t it = w->tok;
            TRY(lex(w));
            if (starts_atom(w)) { /* - f x: the minus applies to the application */
                return tfail(w, ZL_E_UNSUPPORTED, "only a named function can be applied");
            }
            e = mklit(w, E_INT, v, &t);
            (void) it;
        } else {
            int32_t a = p_neg(w);
            TRY(a);
            e = fold_neg(w, a, &t);
        }
        w->depth--;
        return e;
    }
    return p_app(w);
}

/* binary levels; SML infix: 4 comparisons (left), 5 :: @ (right), 6 + - ^
 * (left), 7 * div mod (left). OCaml: comparisons (left), @ ^ (right), ::
 * (right), + - (left), * / mod (left). */
typedef struct {
    int tok, op, prec, right;
} mbop_t;

static const mbop_t sml_ops[] = {
    {T_EQ, O_EQ, 4, 0},     {T_NE, O_NE, 4, 0},     {T_LT, O_LT, 4, 0},
    {T_LE, O_LE, 4, 0},     {T_GT, O_GT, 4, 0},     {T_GE, O_GE, 4, 0},
    {T_CONS, O_CONS, 5, 1}, {T_AT, O_APPEND, 5, 1}, {T_PLUS, O_ADD, 6, 0},
    {T_MINUS, O_SUB, 6, 0}, {T_CARET, O_CAT, 6, 0}, {T_STAR, O_MUL, 7, 0},
    {K_DIV, O_DIV, 7, 0},   {K_MOD, O_MOD, 7, 0},
};
static const mbop_t ocaml_ops[] = {
    {T_EQ, O_EQ, 4, 0},      {T_NE, O_NE, 4, 0},     {T_LT, O_LT, 4, 0},
    {T_LE, O_LE, 4, 0},      {T_GT, O_GT, 4, 0},     {T_GE, O_GE, 4, 0},
    {T_AT, O_APPEND, 5, 1},  {T_CARET, O_CAT, 5, 1}, {T_CONS, O_CONS, 6, 1},
    {T_PLUS, O_ADD, 7, 0},   {T_MINUS, O_SUB, 7, 0}, {T_STAR, O_MUL, 8, 0},
    {T_SLASH, O_DIV, 8, 0},  {K_MOD, O_MOD, 8, 0},
};

static const mbop_t *binop_of(mw_t *w, int tok)
{
    const mbop_t *t = w->dial == D_SML ? sml_ops : ocaml_ops;
    size_t n = w->dial == D_SML ? sizeof sml_ops / sizeof sml_ops[0]
                                : sizeof ocaml_ops / sizeof ocaml_ops[0];
    for (size_t i = 0; i < n; i++)
        if (t[i].tok == tok) return &t[i];
    return 0;
}

static int32_t p_infix(mw_t *w, int minp)
{
    TRY(deeper(w));
    int32_t a = w->dial == D_SML ? p_app(w) : p_neg(w);
    TRY(a);
    for (;;) {
        const mbop_t *o = binop_of(w, w->tok.k);
        if (!o || o->prec < minp) break;
        mtok_t t = w->tok;
        TRY(lex(w));
        int32_t b = p_infix(w, o->right ? o->prec : o->prec + 1);
        TRY(b);
        a = mkbin(w, o->op, a, b, &t);
        TRY(a);
    }
    w->depth--;
    return a;
}

/* SML: andalso binds tighter than orelse; both take open forms on the right */
static int32_t p_logic(mw_t *w, int orl)
{
    int tk = w->dial == D_SML ? (orl ? K_ORELSE : K_ANDALSO) : (orl ? T_OR2 : T_AND2);
    TRY(deeper(w));
    int32_t a = orl ? p_logic(w, 0) : p_infix(w, 0);
    TRY(a);
    while (w->tok.k == tk) {
        mtok_t t = w->tok;
        TRY(lex(w));
        int32_t b;
        if (is_open_start(w))
            b = p_exp(w);
        else if (w->dial == D_OCAML)
            b = p_logic(w, orl); /* OCaml: right associative */
        else
            b = orl ? p_logic(w, 0) : p_infix(w, 0);
        TRY(b);
        a = mkbin(w, orl ? O_ORELSE : O_ANDALSO, a, b, &t);
        TRY(a);
        if (w->dial == D_OCAML) break;
    }
    w->depth--;
    return a;
}

static int32_t p_let_ocaml(mw_t *w, const mtok_t *t);

/* exp: the open forms, else an orelse-expression (OCaml: also a tuple) */
static int32_t p_exp(mw_t *w)
{
    mtok_t t = w->tok;
    int32_t n;
    TRY(deeper(w));
    if (t.k == K_IF) {
        TRY(lex(w));
        n = newnode(w, E_IF, &t);
        TRY(n);
        int32_t a = p_exp(w);
        TRY(a);
        TRY(expect(w, K_THEN, "expected then"));
        int32_t b = p_exp(w);
        TRY(b);
        int32_t c = -1;
        if (w->tok.k == K_ELSE) {
            TRY(lex(w));
            c = p_exp(w);
            TRY(c);
        } else if (w->dial == D_SML) {
            return tfail(w, ZL_E_SYNTAX, "expected else");
        }
        N(w, n)->a = a;
        N(w, n)->b = b;
        N(w, n)->c = c;
    } else if (t.k == K_CASE || t.k == K_MATCH) {
        TRY(lex(w));
        n = newnode(w, E_CASE, &t);
        TRY(n);
        int32_t a = w->dial == D_SML ? p_exp(w) : p_seq(w);
        TRY(a);
        TRY(expect(w, w->dial == D_SML ? K_OF : K_WITH, w->dial == D_SML ? "expected of" : "expected with"));
        int32_t r = p_rules(w);
        TRY(r);
        N(w, n)->a = a;
        N(w, n)->b = r;
    } else if (t.k == K_LET && w->dial == D_OCAML) {
        TRY(lex(w));
        n = p_let_ocaml(w, &t);
        TRY(n);
    } else if (t.k == K_FUNCTION) {
        return tfail(w, ZL_E_UNSUPPORTED, "function (functions as values) is not in this subset");
    } else {
        n = p_logic(w, 1);
        TRY(n);
        if (w->dial == D_OCAML && w->tok.k == T_COMMA) {
            int32_t tu = newnode(w, E_TUPLE, &t);
            TRY(tu);
            mlist_t l = {n, n};
            uint32_t k = 1;
            while (w->tok.k == T_COMMA) {
                TRY(lex(w));
                int32_t q = p_logic(w, 1);
                TRY(q);
                ladd(w, &l, q);
                k++;
            }
            if (k > M_GROUPS) return tfail(w, ZL_E_LIMIT, "tuple too long");
            N(w, tu)->a = l.head;
            N(w, tu)->v = k;
            n = tu;
        }
    }
    w->depth--;
    return n;
}

/* OCaml sequence: e1; e2; ... */
static int32_t p_seq(mw_t *w)
{
    mtok_t t = w->tok;
    int32_t e = p_exp(w);
    TRY(e);
    if (w->dial == D_SML || w->tok.k != T_SEMI) return e;
    int32_t s = newnode(w, E_SEQ, &t);
    TRY(s);
    mlist_t l = {e, e};
    uint32_t k = 1;
    while (w->tok.k == T_SEMI) {
        TRY(lex(w));
        int32_t f = p_exp(w);
        TRY(f);
        ladd(w, &l, f);
        k++;
    }
    N(w, s)->a = l.head;
    N(w, s)->v = k;
    return s;
}

/* function clause parameters: atomic patterns until = */
static int32_t p_params(mw_t *w, uint32_t *k)
{
    mlist_t l = {-1, -1};
    *k = 0;
    while (w->tok.k != T_EQ) {
        if (w->tok.k == T_EOF) return tfail(w, ZL_E_SYNTAX, "expected =");
        int32_t p = p_atpat(w);
        TRY(p);
        ladd(w, &l, p);
        if (++*k > M_GROUPS) return tfail(w, ZL_E_LIMIT, "too many parameters");
    }
    if (*k == 0) return tfail(w, ZL_E_SYNTAX, "a function needs a parameter");
    return l.head;
}

/* SML fun f p.. = e | f p.. = e and g ... */
static int32_t p_fun_sml(mw_t *w, const mtok_t *t)
{
    int32_t d = newnode(w, D_FUN, t);
    TRY(d);
    N(w, d)->flags |= F_REC;
    mlist_t fbs = {-1, -1};
    for (;;) {
        if (w->tok.k != T_ID) return tfail(w, ZL_E_SYNTAX, "expected a function name");
        mtok_t nt = w->tok;
        int32_t fb = newnode(w, N_FB, &nt);
        TRY(fb);
        name_node(w, fb, &nt);
        mlist_t cl = {-1, -1};
        uint32_t np0 = 0;
        for (;;) {
            if (w->tok.k != T_ID || w->tok.len != nt.len) return tfail(w, ZL_E_SYNTAX, "the clauses of a function must all name it");
            for (uint32_t i = 0; i < nt.len; i++)
                if (w->src[w->tok.at + i] != w->src[nt.at + i])
                    return tfail(w, ZL_E_SYNTAX, "the clauses of a function must all name it");
            mtok_t ct = w->tok;
            TRY(lex(w));
            int32_t c = newnode(w, N_CLAUSE, &ct);
            TRY(c);
            uint32_t np;
            int32_t ps = p_params(w, &np);
            TRY(ps);
            if (cl.head >= 0 && np != np0)
                return tfail(w, ZL_E_SYNTAX, "the clauses of a function must have the same number of parameters");
            np0 = np;
            TRY(lex(w)); /* = */
            int32_t e = p_exp(w);
            TRY(e);
            N(w, c)->a = ps;
            N(w, c)->b = e;
            N(w, c)->v = np;
            ladd(w, &cl, c);
            if (w->tok.k != T_BAR) break;
            TRY(lex(w));
        }
        N(w, fb)->a = cl.head;
        ladd(w, &fbs, fb);
        if (w->tok.k != K_AND) break;
        TRY(lex(w));
    }
    N(w, d)->a = fbs.head;
    return d;
}

/* OCaml let bindings after "let": [rec] binding {and binding}. A value
 * binding is D_VAL; function bindings make one D_FUN. */
static int32_t p_bindings_ocaml(mw_t *w, const mtok_t *t)
{
    int rec = 0;
    if (w->tok.k == K_REC) {
        rec = 1;
        TRY(lex(w));
    }
    int32_t d = -1;
    mlist_t fbs = {-1, -1};
    for (;;) {
        mtok_t bt = w->tok;
        int isfun = 0;
        if (bt.k == T_ID) {
            /* look past the name: a parameter or "= function" makes a function */
            mlex_t save = w->lx;
            mtok_t st = w->tok;
            size_t le = w->last_end;
            uint32_t nc = w->ncom;
            TRY(lex(w));
            int k = w->tok.k;
            if (k == T_ID || k == T_WILD || k == T_LP || k == T_LB || k == T_INT || k == K_TRUE ||
                k == K_FALSE || k == T_MINUS || k == T_STR) {
                isfun = 1;
            } else if (k == T_EQ) {
                TRY(lex(w));
                if (w->tok.k == K_FUNCTION) isfun = 2;
            }
            w->lx = save;
            w->tok = st;
            w->last_end = le;
            w->ncom = nc;
        }
        if (!isfun) {
            if (rec) return tfail(w, ZL_E_UNSUPPORTED, "let rec of a value is not in this subset");
            if (fbs.head >= 0) return tfail(w, ZL_E_UNSUPPORTED, "let ... and of values is not in this subset");
            d = newnode(w, D_VAL, t);
            TRY(d);
            int32_t p = p_tpat(w);
            TRY(p);
            TRY(expect(w, T_EQ, "expected ="));
            int32_t e = p_seq(w);
            TRY(e);
            N(w, d)->a = p;
            N(w, d)->b = e;
            if (w->tok.k == K_AND) return tfail(w, ZL_E_UNSUPPORTED, "let ... and of values is not in this subset");
            return d;
        }
        int32_t fb = newnode(w, N_FB, &bt);
        TRY(fb);
        name_node(w, fb, &bt);
        TRY(lex(w));
        if (isfun == 2) {
            TRY(lex(w)); /* = */
            TRY(lex(w)); /* function */
            N(w, fb)->flags |= F_FUNCTION;
            int32_t r = p_rules(w);
            TRY(r);
            /* each rule becomes a clause of one parameter */
            mlist_t cl = {-1, -1};
            for (int32_t x = r; x >= 0;) {
                int32_t nx = N(w, x)->next;
                N(w, x)->kind = N_CLAUSE;
                N(w, x)->next = -1;
                int32_t p = N(w, x)->a, g = N(w, x)->b, e = N(w, x)->c;
                N(w, x)->a = p;
                N(w, x)->b = e;
                N(w, x)->c = g;
                N(w, x)->v = 1;
                ladd(w, &cl, x);
                x = nx;
            }
            N(w, fb)->a = cl.head;
        } else {
            int32_t c = newnode(w, N_CLAUSE, &bt);
            TRY(c);
            uint32_t np;
            int32_t ps = p_params(w, &np);
            TRY(ps);
            TRY(lex(w)); /* = */
            int32_t e = p_seq(w);
            TRY(e);
            N(w, c)->a = ps;
            N(w, c)->b = e;
            N(w, c)->v = np;
            N(w, fb)->a = c;
        }
        ladd(w, &fbs, fb);
        if (w->tok.k != K_AND) break;
        if (!rec) return tfail(w, ZL_E_UNSUPPORTED, "let ... and without rec is not in this subset");
        TRY(lex(w));
    }
    d = newnode(w, D_FUN, t);
    TRY(d);
    if (rec) N(w, d)->flags |= F_REC;
    N(w, d)->a = fbs.head;
    return d;
}

static int32_t p_let_ocaml(mw_t *w, const mtok_t *t)
{
    int32_t d = p_bindings_ocaml(w, t);
    TRY(d);
    TRY(expect(w, K_IN, "expected in"));
    int32_t e = p_seq(w);
    TRY(e);
    int32_t n = newnode(w, E_LET, t);
    TRY(n);
    N(w, n)->a = d;
    N(w, n)->b = e;
    return n;
}

static int32_t p_decl(mw_t *w, int top)
{
    (void) top;
    mtok_t t = w->tok;
    if (t.k == K_VAL) {
        TRY(lex(w));
        int32_t d = newnode(w, D_VAL, &t);
        TRY(d);
        int32_t p = p_pat(w);
        TRY(p);
        TRY(expect(w, T_EQ, "expected ="));
        int32_t e = p_exp(w);
        TRY(e);
        N(w, d)->a = p;
        N(w, d)->b = e;
        if (w->tok.k == K_AND) return tfail(w, ZL_E_UNSUPPORTED, "val ... and is not in this subset");
        return d;
    }
    if (t.k == K_FUN) {
        TRY(lex(w));
        return p_fun_sml(w, &t);
    }
    return tfail(w, ZL_E_SYNTAX, "expected a declaration");
}

static int add_top(mw_t *w, int32_t n)
{
    if (w->ntops >= M_TOPS) return tfail(w, ZL_E_LIMIT, "too many top-level declarations");
    w->tops[w->ntops++] = n;
    return ZL_OK;
}

/* comments that end before `upto` and start after the last declaration */
static int take_comments(mw_t *w, size_t from, size_t upto)
{
    for (; w->ncomuse < w->ncom && w->coms[w->ncomuse].at < upto; w->ncomuse++) {
        if (w->coms[w->ncomuse].at < from) continue;
        mtok_t t = w->tok;
        t.line = w->coms[w->ncomuse].line;
        t.col = 1;
        int32_t n = newnode(w, N_COMMENT, &t);
        TRY(n);
        N(w, n)->v = (int64_t) w->coms[w->ncomuse].at;
        N(w, n)->len = 0;
        N(w, n)->sym = (int32_t) w->coms[w->ncomuse].len;
        TRY(add_top(w, n));
    }
    return ZL_OK;
}

static int p_program(mw_t *w)
{
    TRY(lex(w));
    int sep = 1; /* at the start, or after ; (SML) / ;; (OCaml) */
    size_t prev_end = 0;
    for (;;) {
        if (w->dial == D_SML && w->tok.k == T_SEMI) {
            sep = 1;
            TRY(lex(w));
            continue;
        }
        if (w->dial == D_OCAML && w->tok.k == T_SEMI2) {
            sep = 1;
            TRY(lex(w));
            continue;
        }
        TRY(take_comments(w, prev_end, w->tok.k == T_EOF ? w->len + 1 : w->tok.at));
        if (w->tok.k == T_EOF) break;
        mtok_t t = w->tok;
        int32_t d;
        w->depth = 0;
        if (w->dial == D_SML && (t.k == K_VAL || t.k == K_FUN)) {
            d = p_decl(w, 1);
            TRY(d);
            sep = 0;
        } else if (w->dial == D_OCAML && t.k == K_LET) {
            TRY(lex(w));
            d = p_bindings_ocaml(w, &t);
            TRY(d);
            if (w->tok.k == K_IN) { /* a top-level expression let ... in ... */
                if (!sep) return tfail(w, ZL_E_SYNTAX, "a top-level expression needs ;; before it");
                TRY(lex(w));
                int32_t e = p_seq(w);
                TRY(e);
                int32_t l = newnode(w, E_LET, &t);
                TRY(l);
                N(w, l)->a = d;
                N(w, l)->b = e;
                d = newnode(w, D_EXP, &t);
                TRY(d);
                N(w, d)->a = l;
                N(w, d)->flags |= F_TOP;
            }
            sep = 0;
        } else {
            if (!sep)
                return tfail(w, ZL_E_SYNTAX, w->dial == D_SML ? "a top-level expression needs ; before it"
                                                              : "a top-level expression needs ;; before it");
            int32_t e = w->dial == D_SML ? p_exp(w) : p_seq(w);
            TRY(e);
            d = newnode(w, D_EXP, &t);
            TRY(d);
            N(w, d)->a = e;
            N(w, d)->flags |= F_TOP;
            if (w->dial == D_SML && w->tok.k != T_SEMI)
                return tfail(w, ZL_E_SYNTAX, "expected ; after a top-level expression");
            sep = 0;
        }
        TRY(add_top(w, d));
        prev_end = w->last_end;
        if (w->dial == D_OCAML && !(w->tok.k == T_EOF || w->tok.k == T_SEMI2 || w->tok.k == K_LET))
            return tfail(w, ZL_E_SYNTAX, "unexpected text after a declaration");
        if (w->dial == D_SML && !(w->tok.k == T_EOF || w->tok.k == T_SEMI || w->tok.k == K_VAL ||
                                  w->tok.k == K_FUN))
            return tfail(w, ZL_E_SYNTAX, "unexpected text after a declaration");
    }
    return ZL_OK;
}

/* ------------------------------------------------------------ types */

static int32_t newty(mw_t *w, int k, int32_t a)
{
    if (w->nty >= M_TYPES) return tfail(w, ZL_E_LIMIT, "too many types for the ML front-end");
    int32_t i = (int32_t) w->nty++;
    w->tys[i].k = (uint8_t) k;
    w->tys[i].gen = 0;
    w->tys[i].lvl = (uint16_t) w->tl;
    w->tys[i].a = a;
    return i;
}

static int32_t tvar(mw_t *w)
{
    return newty(w, TY_VAR, -1);
}

static int32_t tfind(mw_t *w, int32_t t)
{
    while (w->tys[t].k == TY_VAR && w->tys[t].a >= 0) t = w->tys[t].a;
    return t;
}

/* occurs check and level adjustment */
static int occurs(mw_t *w, int32_t v, int32_t t, uint16_t lvl)
{
    for (;;) {
        t = tfind(w, t);
        if (t == v) return 1;
        if (w->tys[t].k == TY_VAR) {
            if (w->tys[t].lvl > lvl) w->tys[t].lvl = lvl;
            return 0;
        }
        if (w->tys[t].k != TY_LIST) return 0;
        t = w->tys[t].a;
    }
}

static int unify(mw_t *w, int32_t a, int32_t b)
{
    for (;;) {
        a = tfind(w, a);
        b = tfind(w, b);
        if (a == b) return 1;
        if (w->tys[a].k == TY_VAR) {
            if (occurs(w, a, b, w->tys[a].lvl)) return 0;
            w->tys[a].a = b;
            return 1;
        }
        if (w->tys[b].k == TY_VAR) {
            if (occurs(w, b, a, w->tys[b].lvl)) return 0;
            w->tys[b].a = a;
            return 1;
        }
        if (w->tys[a].k != w->tys[b].k) return 0;
        if (w->tys[a].k != TY_LIST) return 1;
        a = w->tys[a].a;
        b = w->tys[b].a;
    }
}

static int unify_at(mw_t *w, int32_t n, int32_t a, int32_t b, const char *msg)
{
    if (!unify(w, a, b)) return nfail(w, n, ZL_E_SYNTAX, msg);
    return ZL_OK;
}

static void generalize(mw_t *w, int32_t t)
{
    for (;;) {
        t = tfind(w, t);
        if (w->tys[t].k == TY_VAR) {
            if (w->tys[t].lvl > w->tl) w->tys[t].gen = 1;
            return;
        }
        if (w->tys[t].k != TY_LIST) return;
        t = w->tys[t].a;
    }
}

static int32_t inst1(mw_t *w, int32_t t, uint32_t *nm)
{
    t = tfind(w, t);
    if (w->tys[t].k == TY_VAR) {
        if (!w->tys[t].gen) return t;
        for (uint32_t i = 0; i < *nm; i++)
            if (w->imap[i][0] == t) return w->imap[i][1];
        if (*nm >= M_IMAP) return tfail(w, ZL_E_LIMIT, "type too large");
        int32_t f = tvar(w);
        TRY(f);
        w->imap[*nm][0] = t;
        w->imap[*nm][1] = f;
        (*nm)++;
        return f;
    }
    if (w->tys[t].k != TY_LIST) return t;
    int32_t e = inst1(w, w->tys[t].a, nm);
    TRY(e);
    return newty(w, TY_LIST, e);
}

/* ------------------------------------------------------------ names */

static int same_name(mw_t *w, uint32_t pa, uint32_t la, uint32_t pb, uint32_t lb)
{
    if (la != lb) return 0;
    for (uint32_t i = 0; i < la; i++)
        if (w->src[pa + i] != w->src[pb + i]) return 0;
    return 1;
}

static int32_t newbind(mw_t *w, int32_t n, int kind)
{
    if (w->nb >= M_BINDS) return nfail(w, n, ZL_E_LIMIT, "too many names");
    if (w->nsc >= M_SCOPE) return nfail(w, n, ZL_E_LIMIT, "too many names in scope");
    int32_t b = (int32_t) w->nb++;
    mbind_t *x = &w->binds[b];
    zl_bzero(x, sizeof *x);
    x->pos = (uint32_t) N(w, n)->v;
    x->len = N(w, n)->len;
    x->kind = (uint8_t) kind;
    x->ty = x->fn = x->addr = -1;
    x->level = (uint8_t) w->lvl;
    return b;
}

static void push_scope(mw_t *w, int32_t b)
{
    w->scope[w->nsc++] = b;
}

static int32_t lookup(mw_t *w, int32_t n)
{
    uint32_t p = (uint32_t) N(w, n)->v, l = N(w, n)->len;
    for (uint32_t i = w->nsc; i-- > 0;) {
        mbind_t *b = &w->binds[w->scope[i]];
        if (same_name(w, b->pos, b->len, p, l)) return w->scope[i];
    }
    return -1;
}

static const mbi_t *builtin(mw_t *w, int32_t n)
{
    const char *s = w->src + N(w, n)->v;
    size_t l = N(w, n)->len;
    for (size_t i = 0; i < sizeof bis / sizeof bis[0]; i++)
        if ((bis[i].dial & (1 << w->dial)) && spell(s, l, bis[i].s)) return &bis[i];
    return 0;
}

int zl_ml_prelude(mw_t *w, size_t at, uint32_t len)
{
    for (size_t i = 0; i < sizeof bis / sizeof bis[0]; i++)
        if ((bis[i].dial & 4) && spell(w->src + at, len, bis[i].s)) return 1;
    return 0;
}

/* ------------------------------------------------------------ type checking */

static int32_t tc_exp(mw_t *w, int32_t e);
static int tc_decl(mw_t *w, int32_t d);

/* bind the variables of pattern p of type t; tuples only where allowed */
static int tc_pat(mw_t *w, int32_t p, int32_t t)
{
    mnode_t *n = N(w, p);
    n->ty = t;
    if (++w->depth > M_DEPTH) return nfail(w, p, ZL_E_LIMIT, "pattern nested too deeply");
    switch (n->kind) {
    case P_WILD:
        break;
    case P_VAR: {
        int32_t b = newbind(w, p, BK_VAR);
        TRY(b);
        w->binds[b].ty = t;
        n->sym = b;
        push_scope(w, b);
        break;
    }
    case P_INT:
        TRY(unify_at(w, p, t, TINT, "type error: an integer pattern against another type"));
        break;
    case P_BOOL:
        TRY(unify_at(w, p, t, TBOOL, "type error: a boolean pattern against another type"));
        break;
    case P_UNIT:
        TRY(unify_at(w, p, t, TUNIT, "type error: () against another type"));
        break;
    case P_NIL: {
        int32_t e = tvar(w);
        TRY(e);
        int32_t l = newty(w, TY_LIST, e);
        TRY(l);
        TRY(unify_at(w, p, t, l, "type error: a list pattern against another type"));
        break;
    }
    case P_CONS: {
        int32_t e = tvar(w);
        TRY(e);
        int32_t l = newty(w, TY_LIST, e);
        TRY(l);
        TRY(unify_at(w, p, t, l, "type error: a list pattern against another type"));
        TRY(tc_pat(w, n->a, e));
        TRY(tc_pat(w, N(w, p)->b, l));
        break;
    }
    case P_LIST: {
        int32_t e = tvar(w);
        TRY(e);
        int32_t l = newty(w, TY_LIST, e);
        TRY(l);
        TRY(unify_at(w, p, t, l, "type error: a list pattern against another type"));
        for (int32_t q = N(w, p)->a; q >= 0; q = N(w, q)->next) TRY(tc_pat(w, q, e));
        break;
    }
    case P_TUPLE:
        return nfail(w, p, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
    default:
        return nfail(w, p, ZL_E_SYNTAX, "expected a pattern");
    }
    w->depth--;
    return ZL_OK;
}

/* the components of a row: a tuple pattern of k, or a wildcard (all k) */
static int tc_rowpat(mw_t *w, int32_t p, int32_t k, const int32_t *tys)
{
    if (k == 1) return tc_pat(w, p, tys[0]);
    if (N(w, p)->kind == P_WILD) {
        N(w, p)->ty = -1;
        return ZL_OK;
    }
    if (N(w, p)->kind != P_TUPLE) return nfail(w, p, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
    if (N(w, p)->v != k) return nfail(w, p, ZL_E_SYNTAX, "type error: tuples of different lengths");
    int32_t i = 0;
    for (int32_t q = N(w, p)->a; q >= 0; q = N(w, q)->next) TRY(tc_pat(w, q, tys[i++]));
    return ZL_OK;
}

/* the arity of a row of patterns: the tuple length, or 1 */
static uint32_t pat_arity(mw_t *w, int32_t p)
{
    return N(w, p)->kind == P_TUPLE ? (uint32_t) N(w, p)->v : 1;
}

/* Haskell: a value to show (print or show): a tuple only here, written out */
static int tc_shown(mw_t *w, int32_t e)
{
    if (N(w, e)->kind != E_TUPLE) {
        int32_t t = tc_exp(w, e);
        TRY(t);
        return ZL_OK;
    }
    if (++w->depth > M_DEPTH) return nfail(w, e, ZL_E_LIMIT, "expression nested too deeply");
    for (int32_t q = N(w, e)->a; q >= 0; q = N(w, q)->next) TRY(tc_shown(w, q));
    w->depth--;
    return ZL_OK;
}

/* string expressions: literals, toString, ^ */
static int tc_str(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    if (++w->depth > M_DEPTH) return nfail(w, e, ZL_E_LIMIT, "expression nested too deeply");
    if (n->kind == E_STR) {
        n->flags |= F_PRINTED;
    } else if (n->kind == E_BIN && (n->op == O_CAT || (n->op == O_APPEND && w->dial == D_HS))) {
        TRY(tc_str(w, n->a));
        TRY(tc_str(w, N(w, e)->b));
    } else if (w->dial == D_HS && n->kind == E_APP && lookup(w, e) < 0 && builtin(w, e) &&
               builtin(w, e)->id == B_SHOW) {
        if (n->a < 0 || N(w, n->a)->next >= 0)
            return nfail(w, e, ZL_E_SYNTAX, "type error: show takes one argument");
        n->sym = -(int32_t) B_SHOW;
        TRY(tc_shown(w, n->a));
    } else if (n->kind == E_APP && lookup(w, e) < 0 && builtin(w, e) &&
               (builtin(w, e)->id == B_ITOS || builtin(w, e)->id == B_BTOS)) {
        const mbi_t *bi = builtin(w, e);
        if (n->a < 0 || N(w, n->a)->next >= 0)
            return nfail(w, e, ZL_E_SYNTAX, "type error: toString takes one argument");
        n->sym = -(int32_t) bi->id;
        int32_t t = tc_exp(w, n->a);
        TRY(t);
        TRY(unify_at(w, e, t, bi->id == B_ITOS ? TINT : TBOOL,
                     "type error: the argument of toString has another type"));
    } else if (n->kind == E_INT || n->kind == E_BOOL || n->kind == E_UNIT || n->kind == E_NIL ||
               n->kind == E_LIST) {
        return nfail(w, e, ZL_E_SYNTAX, "type error: a string was expected");
    } else if (w->dial == D_HS) {
        return nfail(w, e, ZL_E_UNSUPPORTED,
                     "only literals, show and ++ make strings in this subset (strings are printed, not kept)");
    } else {
        return nfail(w, e, ZL_E_UNSUPPORTED,
                     "only literals, toString and ^ make strings in this subset (strings are printed, not kept)");
    }
    w->depth--;
    return ZL_OK;
}

static int32_t tc_tuple_args(mw_t *w, int32_t a, uint32_t k, int32_t *tys)
{
    if (k == 1) {
        if (N(w, a)->kind == E_TUPLE) return nfail(w, a, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
        tys[0] = tc_exp(w, a);
        TRY(tys[0]);
        return ZL_OK;
    }
    if (N(w, a)->kind != E_TUPLE)
        return nfail(w, a, ZL_E_UNSUPPORTED, "a tuple argument must be written out as (a, b) in this subset");
    if (N(w, a)->v != k) return nfail(w, a, ZL_E_SYNTAX, "type error: a tuple of the wrong length");
    uint32_t i = 0;
    for (int32_t q = N(w, a)->a; q >= 0; q = N(w, q)->next) {
        tys[i] = tc_exp(w, q);
        TRY(tys[i]);
        i++;
    }
    return ZL_OK;
}

/* a fresh list type; *el gets its element type */
static int32_t tlist(mw_t *w, int32_t *el)
{
    *el = tvar(w);
    TRY(*el);
    return newty(w, TY_LIST, *el);
}

/* the Haskell Prelude functions (argument counts already checked) */
static int32_t tc_hs_builtin(mw_t *w, int32_t e, int id)
{
    int32_t a0 = N(w, e)->a, b0 = N(w, a0)->next, tys[2], el, l;
    if (id == B_HPRINT) {
        TRY(tc_shown(w, a0));
        return TIO;
    }
    TRY(tc_tuple_args(w, a0, 1, tys));
    if (b0 >= 0) TRY(tc_tuple_args(w, b0, 1, tys + 1));
    switch (id) {
    case B_HPRINT:
        return TIO;
    case B_SHOW:
        return nfail(w, e, ZL_E_UNSUPPORTED, "strings as values are not in this subset (only printed)");
    case B_RETURN:
        if (N(w, a0)->kind != E_UNIT)
            return nfail(w, a0, ZL_E_UNSUPPORTED, "return of a value other than () is not in this subset");
        return TIO;
    case B_SIGNUM:
    case B_NEGATE:
    case B_EVEN:
    case B_ODD:
        TRY(unify_at(w, a0, tys[0], TINT, "type error: an integer was expected"));
        return id == B_EVEN || id == B_ODD ? TBOOL : TINT;
    case B_GCD:
    case B_LCM:
        TRY(unify_at(w, a0, tys[0], TINT, "type error: an integer was expected"));
        TRY(unify_at(w, b0, tys[1], TINT, "type error: an integer was expected"));
        return TINT;
    default:
        break;
    }
    w->lists = 1;
    switch (id) {
    case B_SUM:
    case B_PRODUCT:
    case B_MAXIMUM:
    case B_MINIMUM:
        l = newty(w, TY_LIST, TINT);
        TRY(l);
        TRY(unify_at(w, a0, tys[0], l, "type error: a list of integers was expected"));
        return TINT;
    case B_AND:
    case B_OR:
        l = newty(w, TY_LIST, TBOOL);
        TRY(l);
        TRY(unify_at(w, a0, tys[0], l, "type error: a list of booleans was expected"));
        return TBOOL;
    case B_TAKE:
    case B_DROP:
        TRY(unify_at(w, a0, tys[0], TINT, "type error: an integer was expected"));
        l = tlist(w, &el);
        TRY(l);
        TRY(unify_at(w, b0, tys[1], l, "type error: a list was expected"));
        return l;
    case B_ELEM:
    case B_NOTELEM:
        l = newty(w, TY_LIST, tys[0]);
        TRY(l);
        TRY(unify_at(w, b0, tys[1], l, "type error: elem takes a value and a list of its type"));
        return TBOOL;
    case B_REPLICATE:
        TRY(unify_at(w, a0, tys[0], TINT, "type error: an integer was expected"));
        return newty(w, TY_LIST, tys[1]);
    case B_LAST:
    case B_INIT:
        l = tlist(w, &el);
        TRY(l);
        TRY(unify_at(w, a0, tys[0], l, "type error: a list was expected"));
        return id == B_LAST ? el : l;
    case B_CONCAT: {
        l = tlist(w, &el);
        TRY(l);
        int32_t ll = newty(w, TY_LIST, l);
        TRY(ll);
        TRY(unify_at(w, a0, tys[0], ll, "type error: a list of lists was expected"));
        return l;
    }
    default:
        break;
    }
    return nfail(w, e, ZL_E_UNSUPPORTED, "this library function is not in this subset");
}

static int32_t tc_builtin_app(mw_t *w, int32_t e, const mbi_t *bi)
{
    mnode_t *n = N(w, e);
    uint32_t nargs = 0;
    for (int32_t q = n->a; q >= 0; q = N(w, q)->next) nargs++;
    n->sym = -(int32_t) bi->id;
    int32_t a0 = n->a, tys[M_GROUPS];
    if (bi->id == B_PRINTF) {
        if (N(w, a0)->kind != E_STR)
            return nfail(w, e, ZL_E_UNSUPPORTED, "Printf.printf needs a literal format");
        N(w, a0)->flags |= F_PRINTED;
        /* count the conversions: %d %i %b %s %% */
        const char *s = w->src + N(w, a0)->v + 1;
        uint32_t len = N(w, a0)->len - 2;
        int32_t arg = N(w, a0)->next;
        for (uint32_t i = 0; i < len; i++) {
            if (s[i] == '\\') {
                i++;
                continue;
            }
            if (s[i] != '%') continue;
            if (++i >= len) return nfail(w, a0, ZL_E_SYNTAX, "a format ends in %");
            char f = s[i];
            if (f == '%') continue;
            if (f != 'd' && f != 'i' && f != 'b' && f != 's')
                return nfail(w, a0, ZL_E_UNSUPPORTED, "only %d %i %b %s and %% are in this subset's formats");
            if (arg < 0) return nfail(w, e, ZL_E_UNSUPPORTED, "partial application of Printf.printf is not in this subset");
            if (N(w, arg)->kind == E_TUPLE) return nfail(w, arg, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
            if (f == 's') {
                TRY(tc_str(w, arg));
            } else {
                int32_t t = tc_exp(w, arg);
                TRY(t);
                TRY(unify_at(w, arg, t, f == 'b' ? TBOOL : TINT, "type error: a printf argument has another type"));
            }
            arg = N(w, arg)->next;
        }
        if (arg >= 0) return nfail(w, arg, ZL_E_SYNTAX, "type error: too many arguments for the format");
        return TUNIT;
    }
    if (bi->ng == 0) return nfail(w, e, ZL_E_SYNTAX, "type error: this value is not a function");
    if (nargs < bi->ng) return nfail(w, e, ZL_E_UNSUPPORTED, "partial application is not in this subset");
    if (nargs > bi->ng) return nfail(w, e, ZL_E_SYNTAX, "type error: too many arguments");
    if (w->dial == D_HS && bi->id >= B_HPRINT) return tc_hs_builtin(w, e, bi->id);
    switch (bi->id) {
    case B_PRINT:
    case B_PRINT_STRING:
    case B_PRINT_ENDLINE:
        TRY(tc_str(w, a0));
        return w->dial == D_HS ? TIO : TUNIT;
    case B_ITOS:
    case B_BTOS:
        return nfail(w, e, ZL_E_UNSUPPORTED, "strings as values are not in this subset (only printed)");
    case B_PRINT_INT:
    case B_ABS:
    case B_SUCC:
    case B_PRED: {
        TRY(tc_tuple_args(w, a0, 1, tys));
        TRY(unify_at(w, a0, tys[0], TINT, "type error: an integer was expected"));
        return bi->id == B_PRINT_INT ? TUNIT : TINT;
    }
    case B_PRINT_NEWLINE:
        TRY(tc_tuple_args(w, a0, 1, tys));
        TRY(unify_at(w, a0, tys[0], TUNIT, "type error: print_newline takes ()"));
        return TUNIT;
    case B_NOT:
        TRY(tc_tuple_args(w, a0, 1, tys));
        TRY(unify_at(w, a0, tys[0], TBOOL, "type error: not takes a boolean"));
        return TBOOL;
    case B_MIN:
    case B_MAX:
    case B_QUOT:
    case B_REM:
        if (w->dial == D_SML) {
            TRY(tc_tuple_args(w, a0, 2, tys));
            TRY(unify_at(w, a0, tys[0], TINT, "type error: an integer was expected"));
            TRY(unify_at(w, a0, tys[1], TINT, "type error: an integer was expected"));
            return TINT;
        } else {
            int32_t b0 = N(w, a0)->next;
            TRY(tc_tuple_args(w, a0, 1, tys));
            TRY(tc_tuple_args(w, b0, 1, tys + 1));
            TRY(unify_at(w, b0, tys[0], tys[1], "type error: min and max take two values of one type"));
            return tys[0];
        }
    case B_HD:
    case B_TL:
    case B_NULL:
    case B_LENGTH:
    case B_REV: {
        w->lists = 1;
        TRY(tc_tuple_args(w, a0, 1, tys));
        int32_t el = tvar(w);
        TRY(el);
        int32_t l = newty(w, TY_LIST, el);
        TRY(l);
        TRY(unify_at(w, a0, tys[0], l, "type error: a list was expected"));
        return bi->id == B_HD ? el : bi->id == B_NULL ? TBOOL : bi->id == B_LENGTH ? TINT : l;
    }
    default:
        break;
    }
    return nfail(w, e, ZL_E_UNSUPPORTED, "this library function is not in this subset");
}

static int32_t tc_app(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    int32_t b = lookup(w, e);
    if (b < 0) {
        const mbi_t *bi = builtin(w, e);
        if (!bi && n->len && w->src[n->v] >= 'A' && w->src[n->v] <= 'Z')
            return nfail(w, e, ZL_E_UNSUPPORTED, "this library function is not in this subset");
        if (!bi) return nfail(w, e, ZL_E_UNDEFINED, "unbound function name");
        if (!bi->id) return nfail(w, e, ZL_E_UNSUPPORTED, bi->msg);
        return tc_builtin_app(w, e, bi);
    }
    if (w->binds[b].kind != BK_FN) {
        int32_t t = tfind(w, w->binds[b].ty);
        if (w->tys[t].k != TY_VAR) return nfail(w, e, ZL_E_SYNTAX, "type error: a value that is not a function is applied");
        return nfail(w, e, ZL_E_UNSUPPORTED, "calling a parameter (functions as values) is not in this subset");
    }
    n->sym = b;
    mfn_t *f = &w->fns[w->binds[b].fn];
    uint32_t nargs = 0;
    for (int32_t q = n->a; q >= 0; q = N(w, q)->next) nargs++;
    if (nargs < f->ng) return nfail(w, e, ZL_E_UNSUPPORTED, "partial application is not in this subset");
    if (nargs > f->ng) return nfail(w, e, ZL_E_SYNTAX, "type error: too many arguments");
    /* the signature, instantiated when the function is generic */
    int32_t sig[M_GROUPS * M_GROUPS + 1];
    uint32_t nm = 0;
    for (uint32_t i = 0; i < f->np; i++) {
        sig[i] = f->gen ? inst1(w, w->ptys[f->p0 + i], &nm) : w->ptys[f->p0 + i];
        TRY(sig[i]);
    }
    int32_t r = f->gen ? inst1(w, f->rty, &nm) : f->rty;
    TRY(r);
    f = &w->fns[w->binds[b].fn];
    uint32_t k = 0, g = 0;
    for (int32_t q = N(w, e)->a; q >= 0; q = N(w, q)->next, g++) {
        int32_t tys[M_GROUPS];
        uint32_t ar = f->gar[g];
        TRY(tc_tuple_args(w, q, ar, tys));
        for (uint32_t i = 0; i < ar; i++, k++)
            TRY(unify_at(w, q, tys[i], sig[k], "type error: an argument has the wrong type"));
    }
    return r;
}

static int32_t tc_bin(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    int op = n->op;
    if (op == O_CAT) return nfail(w, e, ZL_E_UNSUPPORTED, "strings as values are not in this subset (only printed)");
    if (op == O_CONS || op == O_APPEND || op == O_INDEX) w->lists = 1;
    int32_t a = tc_exp(w, n->a);
    TRY(a);
    int32_t b = tc_exp(w, N(w, e)->b);
    TRY(b);
    switch (op) {
    case O_INDEX: {
        int32_t el, l = tlist(w, &el);
        TRY(l);
        TRY(unify_at(w, e, a, l, "type error: !! needs a list"));
        TRY(unify_at(w, e, b, TINT, "type error: !! needs an integer index"));
        return el;
    }
    case O_ADD:
    case O_SUB:
    case O_MUL:
    case O_DIV:
    case O_MOD:
    case O_QUOT:
    case O_REM:
    case O_POW:
        TRY(unify_at(w, e, a, TINT, "type error: arithmetic on a value that is not an integer"));
        TRY(unify_at(w, e, b, TINT, "type error: arithmetic on a value that is not an integer"));
        return TINT;
    case O_EQ:
    case O_NE:
        TRY(unify_at(w, e, a, b, "type error: comparing values of different types"));
        return TBOOL;
    case O_LT:
    case O_LE:
    case O_GT:
    case O_GE:
        TRY(unify_at(w, e, a, b, "type error: comparing values of different types"));
        if (w->dial == D_SML) TRY(unify_at(w, e, a, TINT, "type error: ordering a value that is not an integer"));
        return TBOOL;
    case O_ANDALSO:
    case O_ORELSE:
        TRY(unify_at(w, e, a, TBOOL, "type error: a logical operator on a value that is not boolean"));
        TRY(unify_at(w, e, b, TBOOL, "type error: a logical operator on a value that is not boolean"));
        return TBOOL;
    case O_CONS: {
        int32_t l = newty(w, TY_LIST, a);
        TRY(l);
        TRY(unify_at(w, e, b, l, "type error: :: needs a list of the element's type"));
        return l;
    }
    case O_APPEND: {
        int32_t el = tvar(w);
        TRY(el);
        int32_t l = newty(w, TY_LIST, el);
        TRY(l);
        TRY(unify_at(w, e, a, l, "type error: @ needs lists"));
        TRY(unify_at(w, e, b, l, "type error: @ needs lists of one type"));
        return l;
    }
    default:
        break;
    }
    return nfail(w, e, ZL_E_SYNTAX, "unknown operator");
}

/* the rules of a case: scrutinee types tys[0..k) */
static int tc_rules(mw_t *w, int32_t r, uint32_t k, const int32_t *tys, int32_t res)
{
    for (; r >= 0; r = N(w, r)->next) {
        uint32_t sc = w->nsc;
        TRY(tc_rowpat(w, N(w, r)->a, (int32_t) k, tys));
        if (N(w, r)->b >= 0) {
            int32_t g = tc_exp(w, N(w, r)->b);
            TRY(g);
            TRY(unify_at(w, N(w, r)->b, g, TBOOL, "type error: a guard must be boolean"));
        }
        int32_t t = tc_exp(w, N(w, r)->c);
        TRY(t);
        TRY(unify_at(w, N(w, r)->c, t, res, "type error: the rules give values of different types"));
        w->nsc = sc;
    }
    return ZL_OK;
}

static int32_t tc_exp(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    int32_t t = -1;
    if (++w->depth > M_DEPTH) return nfail(w, e, ZL_E_LIMIT, "expression nested too deeply");
    switch (n->kind) {
    case E_INT:
        t = TINT;
        break;
    case E_BOOL:
        t = TBOOL;
        break;
    case E_UNIT:
        t = TUNIT;
        break;
    case E_NIL: {
        int32_t el = tvar(w);
        TRY(el);
        t = newty(w, TY_LIST, el);
        TRY(t);
        break;
    }
    case E_STR:
        return nfail(w, e, ZL_E_UNSUPPORTED, "strings as values are not in this subset (only printed)");
    case E_VAR: {
        int32_t b = lookup(w, e);
        if (b < 0) {
            const mbi_t *bi = builtin(w, e);
            if (!bi && N(w, e)->len && w->src[N(w, e)->v] >= 'A' && w->src[N(w, e)->v] <= 'Z')
                return nfail(w, e, ZL_E_UNSUPPORTED, "this library function is not in this subset");
            if (!bi) return nfail(w, e, ZL_E_UNDEFINED, "unbound name");
            if (!bi->id) return nfail(w, e, ZL_E_UNSUPPORTED, bi->msg);
            if (bi->ng) return nfail(w, e, ZL_E_UNSUPPORTED, "a function used as a value is not in this subset");
            n->sym = -(int32_t) bi->id;
            t = bi->id == B_OTHERWISE ? TBOOL : TINT;
            break;
        }
        if (w->binds[b].kind == BK_FN)
            return nfail(w, e, ZL_E_UNSUPPORTED, "a function used as a value is not in this subset");
        n->sym = b;
        if (w->binds[b].gen) {
            uint32_t nm = 0;
            t = inst1(w, w->binds[b].ty, &nm);
            TRY(t);
        } else {
            t = w->binds[b].ty;
        }
        break;
    }
    case E_LIST: {
        int32_t el = tvar(w);
        TRY(el);
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            if (N(w, q)->kind == E_TUPLE) return nfail(w, q, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
            int32_t x = tc_exp(w, q);
            TRY(x);
            TRY(unify_at(w, q, x, el, "type error: list elements of different types"));
        }
        t = newty(w, TY_LIST, el);
        TRY(t);
        break;
    }
    case E_TUPLE:
        return nfail(w, e, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
    case E_BIN:
        if (N(w, n->a)->kind == E_TUPLE) return nfail(w, n->a, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
        if (N(w, n->b)->kind == E_TUPLE) return nfail(w, n->b, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
        t = tc_bin(w, e);
        TRY(t);
        break;
    case E_NEG: {
        int32_t a = tc_exp(w, n->a);
        TRY(a);
        TRY(unify_at(w, e, a, TINT, "type error: negating a value that is not an integer"));
        t = TINT;
        break;
    }
    case E_APP:
        t = tc_app(w, e);
        TRY(t);
        break;
    case E_IF: {
        int32_t a = tc_exp(w, n->a);
        TRY(a);
        TRY(unify_at(w, N(w, e)->a, a, TBOOL, "type error: the condition of if must be boolean"));
        int32_t b = tc_exp(w, N(w, e)->b);
        TRY(b);
        if (N(w, e)->c < 0) {
            TRY(unify_at(w, N(w, e)->b, b, TUNIT, "type error: if without else must give ()"));
            t = TUNIT;
        } else {
            int32_t c = tc_exp(w, N(w, e)->c);
            TRY(c);
            TRY(unify_at(w, N(w, e)->c, b, c, "type error: the branches of if have different types"));
            t = b;
        }
        break;
    }
    case E_CASE: {
        int32_t s = n->a, tys[M_GROUPS];
        uint32_t k = 1;
        if (N(w, s)->kind == E_TUPLE) {
            k = (uint32_t) N(w, s)->v;
            uint32_t i = 0;
            for (int32_t q = N(w, s)->a; q >= 0; q = N(w, q)->next) {
                tys[i] = tc_exp(w, q);
                TRY(tys[i]);
                i++;
            }
        } else {
            tys[0] = tc_exp(w, s);
            TRY(tys[0]);
        }
        t = tvar(w);
        TRY(t);
        TRY(tc_rules(w, N(w, e)->b, k, tys, t));
        break;
    }
    case E_LET: {
        uint32_t sc = w->nsc;
        for (int32_t d = n->a; d >= 0; d = N(w, d)->next) TRY(tc_decl(w, d));
        t = tc_exp(w, N(w, e)->b);
        TRY(t);
        w->nsc = sc;
        break;
    }
    case E_SEQ:
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            if (N(w, q)->kind == E_TUPLE) return nfail(w, q, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
            t = tc_exp(w, q);
            TRY(t);
            if (w->dial == D_HS)
                TRY(unify_at(w, q, t, TIO, "type error: a statement of a do block must be an IO action"));
        }
        break;
    case E_GUARDS:
        t = tvar(w);
        TRY(t);
        for (int32_t g = n->a; g >= 0; g = N(w, g)->next) {
            int32_t c = tc_exp(w, N(w, g)->a);
            TRY(c);
            TRY(unify_at(w, N(w, g)->a, c, TBOOL, "type error: a guard must be boolean"));
            int32_t b = tc_exp(w, N(w, g)->b);
            TRY(b);
            TRY(unify_at(w, N(w, g)->b, b, t, "type error: the guarded bodies give values of different types"));
        }
        break;
    case E_RANGE:
        w->lists = 1;
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            int32_t x = tc_exp(w, q);
            TRY(x);
            TRY(unify_at(w, q, x, TINT, "type error: a range of values that are not integers"));
        }
        t = newty(w, TY_LIST, TINT);
        TRY(t);
        break;
    default:
        return nfail(w, e, ZL_E_SYNTAX, "expected an expression");
    }
    N(w, e)->ty = t;
    w->depth--;
    return t;
}

/* a value in the sense of the value restriction */
static int nonexpansive(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    switch (n->kind) {
    case E_INT:
    case E_BOOL:
    case E_UNIT:
    case E_NIL:
    case E_VAR:
        return 1;
    case E_LIST:
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next)
            if (!nonexpansive(w, q)) return 0;
        return 1;
    case E_BIN:
        return n->op == O_CONS && nonexpansive(w, n->a) && nonexpansive(w, n->b);
    default:
        return 0;
    }
}

static void gen_pat_vars(mw_t *w, int32_t p)
{
    mnode_t *n = N(w, p);
    switch (n->kind) {
    case P_VAR:
        generalize(w, w->binds[n->sym].ty);
        w->binds[n->sym].gen = 1;
        break;
    case P_CONS:
        gen_pat_vars(w, n->a);
        gen_pat_vars(w, n->b);
        break;
    case P_LIST:
    case P_TUPLE:
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) gen_pat_vars(w, q);
        break;
    default:
        break;
    }
}

/* ---- Haskell type signatures ---- */

/* a signature's type as a type term; its type variables by name go to
 * w->rig[r0 ..) (rigid: they must stay distinct and general) */
static int32_t sig_ty(mw_t *w, int32_t t, uint32_t r0)
{
    mnode_t *n = N(w, t);
    switch (n->op) {
    case TK_INT:
        return TINT;
    case TK_BOOL:
        return TBOOL;
    case TK_UNIT:
        return TUNIT;
    case TK_IO:
        return TIO;
    case TK_LIST: {
        int32_t e = sig_ty(w, n->a, r0);
        TRY(e);
        return newty(w, TY_LIST, e);
    }
    case TK_VAR:
        for (uint32_t i = r0; i < w->nrig; i++)
            if (same_name(w, (uint32_t) N(w, w->rign[i])->v, N(w, w->rign[i])->len, (uint32_t) n->v, n->len))
                return w->rig[i];
        if (w->nrig >= M_IMAP) return nfail(w, t, ZL_E_LIMIT, "too many type variables");
        w->rign[w->nrig] = t;
        w->rig[w->nrig] = tvar(w);
        TRY(w->rig[w->nrig]);
        return w->rig[w->nrig++];
    case TK_FUN:
        return nfail(w, t, ZL_E_UNSUPPORTED, "functions as values (a function type here) are not in this subset");
    default:
        return nfail(w, t, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
    }
}

/* the signature t of function fi: one parameter group per arrow */
static int sig_fun(mw_t *w, int32_t fi, int32_t t, uint32_t r0)
{
    mfn_t *f = &w->fns[fi];
    uint32_t k = 0;
    for (uint32_t g = 0; g < f->ng; g++) {
        if (N(w, t)->op != TK_FUN)
            return nfail(w, t, ZL_E_SYNTAX, "type error: the equations have more arguments than the signature");
        int32_t a = N(w, t)->a;
        uint32_t ar = w->fns[fi].gar[g];
        if (ar == 1) {
            int32_t x = sig_ty(w, a, r0);
            TRY(x);
            TRY(unify_at(w, a, w->ptys[w->fns[fi].p0 + k], x, "type error: an argument does not match the signature"));
            k++;
        } else {
            if (N(w, a)->op != TK_TUPLE || N(w, a)->v != ar)
                return nfail(w, a, ZL_E_SYNTAX, "type error: a tuple argument does not match the signature");
            for (int32_t q = N(w, a)->a; q >= 0; q = N(w, q)->next, k++) {
                int32_t x = sig_ty(w, q, r0);
                TRY(x);
                TRY(unify_at(w, q, w->ptys[w->fns[fi].p0 + k], x, "type error: an argument does not match the signature"));
            }
        }
        t = N(w, t)->b;
    }
    if (N(w, t)->op == TK_FUN)
        return nfail(w, t, ZL_E_UNSUPPORTED, "a function whose result is a function (partial application) is not in this subset");
    int32_t r = sig_ty(w, t, r0);
    TRY(r);
    return unify_at(w, t, w->fns[fi].rty, r, "type error: the result does not match the signature");
}

/* after generalization: each signature variable is still a distinct general variable */
static int sig_rigid(mw_t *w, uint32_t r0)
{
    for (uint32_t i = r0; i < w->nrig; i++) {
        int32_t t = tfind(w, w->rig[i]);
        if (w->tys[t].k != TY_VAR || !w->tys[t].gen)
            return nfail(w, w->rign[i], ZL_E_SYNTAX, "type error: a type variable of the signature is not general");
        for (uint32_t j = r0; j < i; j++)
            if (tfind(w, w->rig[j]) == t)
                return nfail(w, w->rign[i], ZL_E_SYNTAX, "type error: two type variables of the signature are the same");
    }
    w->nrig = r0;
    return ZL_OK;
}

static int tc_fun(mw_t *w, int32_t d)
{
    int rec = (N(w, d)->flags & F_REC) != 0;
    uint32_t f0 = w->nf, r0 = w->nrig;
    w->tl++;
    /* the functions of the group, their arities and fresh signatures */
    for (int32_t fb = N(w, d)->a; fb >= 0; fb = N(w, fb)->next) {
        if (w->nf >= M_FNS) return nfail(w, fb, ZL_E_LIMIT, "too many functions");
        int32_t fi = (int32_t) w->nf++;
        mfn_t *f = &w->fns[fi];
        zl_bzero(f, sizeof *f);
        f->fb = fb;
        f->entry = f->enter_at = f->start = -1;
        f->level = (uint8_t) (w->lvl + 1);
        N(w, fb)->sym = fi;
        int32_t c0 = N(w, fb)->a;
        f->ng = (uint16_t) N(w, c0)->v;
        uint32_t np = 0;
        for (uint32_t g = 0; g < f->ng; g++) {
            uint32_t ar = 1;
            for (int32_t c = c0; c >= 0; c = N(w, c)->next) {
                int32_t p = N(w, c)->a;
                for (uint32_t i = 0; i < g; i++) p = N(w, p)->next;
                uint32_t a = pat_arity(w, p);
                if (a > 1) {
                    if (ar > 1 && a != ar) return nfail(w, p, ZL_E_SYNTAX, "type error: tuples of different lengths");
                    ar = a;
                }
            }
            for (int32_t c = c0; c >= 0; c = N(w, c)->next) {
                int32_t p = N(w, c)->a;
                for (uint32_t i = 0; i < g; i++) p = N(w, p)->next;
                if (ar > 1 && N(w, p)->kind != P_TUPLE && N(w, p)->kind != P_WILD)
                    return nfail(w, p, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
            }
            f->gar[g] = (uint8_t) ar;
            np += ar;
        }
        if (np > 64) return nfail(w, fb, ZL_E_LIMIT, "too many parameters");
        if (w->npty + np > M_PTYS) return nfail(w, fb, ZL_E_LIMIT, "too many parameters");
        f->p0 = (int32_t) w->npty;
        f->np = (uint16_t) np;
        for (uint32_t i = 0; i < np; i++) {
            int32_t t = tvar(w);
            TRY(t);
            w->ptys[w->npty++] = t;
        }
        f->rty = tvar(w);
        TRY(f->rty);
        if (N(w, fb)->c >= 0) TRY(sig_fun(w, fi, N(w, fb)->c, w->nrig));
    }
    /* recursive: the names are visible in the bodies */
    uint32_t fi = f0;
    if (rec)
        for (int32_t fb = N(w, d)->a; fb >= 0; fb = N(w, fb)->next, fi++) {
            int32_t b = newbind(w, fb, BK_FN);
            TRY(b);
            w->binds[b].fn = (int32_t) fi;
            w->fns[fi].bind = b;
            push_scope(w, b);
        }
    fi = f0;
    w->lvl++;
    for (int32_t fb = N(w, d)->a; fb >= 0; fb = N(w, fb)->next, fi++) {
        mfn_t *f = &w->fns[fi];
        for (int32_t c = N(w, fb)->a; c >= 0; c = N(w, c)->next) {
            uint32_t sc = w->nsc, k = 0, g = 0;
            for (int32_t p = N(w, c)->a; p >= 0; p = N(w, p)->next, g++) {
                TRY(tc_rowpat(w, p, w->fns[fi].gar[g], &w->ptys[w->fns[fi].p0 + k]));
                k += w->fns[fi].gar[g];
            }
            if (N(w, c)->c >= 0) { /* a guard (OCaml function ... when) */
                int32_t gt = tc_exp(w, N(w, c)->c);
                TRY(gt);
                TRY(unify_at(w, N(w, c)->c, gt, TBOOL, "type error: a guard must be boolean"));
            }
            int32_t t = tc_exp(w, N(w, c)->b);
            TRY(t);
            TRY(unify_at(w, N(w, c)->b, t, w->fns[fi].rty, "type error: the clauses give values of different types"));
            w->nsc = sc;
        }
        (void) f;
    }
    w->lvl--;
    w->tl--;
    for (fi = f0; fi < w->nf; fi++) {
        mfn_t *f = &w->fns[fi];
        for (uint32_t i = 0; i < f->np; i++) generalize(w, w->ptys[f->p0 + i]);
        generalize(w, f->rty);
        f->gen = 1;
    }
    TRY(sig_rigid(w, r0));
    if (!rec) {
        fi = f0;
        for (int32_t fb = N(w, d)->a; fb >= 0; fb = N(w, fb)->next, fi++) {
            int32_t b = newbind(w, fb, BK_FN);
            TRY(b);
            w->binds[b].fn = (int32_t) fi;
            w->fns[fi].bind = b;
            push_scope(w, b);
        }
    }
    return ZL_OK;
}

/* a Haskell value binding x = e: a thunk, run when x is first used */
static int tc_lazy(mw_t *w, int32_t d)
{
    int32_t p = N(w, d)->a, e = N(w, d)->b, sig = N(w, d)->c;
    uint32_t r0 = w->nrig;
    if (w->nf >= M_FNS) return nfail(w, d, ZL_E_LIMIT, "too many functions");
    int32_t fi = (int32_t) w->nf++;
    mfn_t *f = &w->fns[fi];
    zl_bzero(f, sizeof *f);
    f->fb = d;
    f->entry = f->enter_at = f->start = -1;
    f->level = (uint8_t) (w->lvl + 1);
    f->p0 = (int32_t) w->npty;
    N(w, d)->sym = fi;
    w->tl++;
    w->lvl++;
    int32_t t = tc_exp(w, e);
    TRY(t);
    if (sig >= 0) {
        int32_t st = sig_ty(w, sig, r0);
        TRY(st);
        TRY(unify_at(w, sig, t, st, "type error: the value does not match the signature"));
    }
    w->lvl--;
    w->tl--;
    w->fns[fi].rty = t;
    generalize(w, t);
    w->fns[fi].gen = 1;
    TRY(sig_rigid(w, r0));
    int32_t b = newbind(w, p, BK_LAZY);
    TRY(b);
    w->binds[b].fn = fi;
    w->binds[b].ty = t;
    w->binds[b].gen = 1;
    w->fns[fi].bind = b;
    N(w, p)->sym = b;
    N(w, p)->ty = t;
    push_scope(w, b);
    return ZL_OK;
}

static int tc_decl(mw_t *w, int32_t d)
{
    mnode_t *n = N(w, d);
    if (n->kind == D_FUN) return tc_fun(w, d);
    if (n->kind == D_VAL && (n->flags & F_LAZY)) return tc_lazy(w, d);
    if (n->kind == D_EXP) {
        int32_t t = tc_exp(w, n->a);
        TRY(t);
        N(w, d)->ty = t;
        return ZL_OK;
    }
    /* D_VAL */
    int32_t p = n->a, e = n->b;
    if (N(w, e)->kind == E_TUPLE || N(w, p)->kind == P_TUPLE) {
        if (N(w, e)->kind != E_TUPLE && N(w, p)->kind == P_TUPLE)
            return nfail(w, e, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
        if (N(w, p)->kind != P_TUPLE && N(w, p)->kind != P_WILD)
            return nfail(w, p, ZL_E_UNSUPPORTED, "tuples as values are not in this subset");
        uint32_t k = (uint32_t) N(w, e)->v, i = 0;
        int32_t tys[M_GROUPS];
        for (int32_t q = N(w, e)->a; q >= 0; q = N(w, q)->next) {
            tys[i] = tc_exp(w, q);
            TRY(tys[i]);
            i++;
        }
        return tc_rowpat(w, p, (int32_t) k, tys);
    }
    int ne = nonexpansive(w, e);
    if (ne) w->tl++;
    int32_t t = tc_exp(w, e);
    TRY(t);
    if (ne) w->tl--;
    TRY(tc_pat(w, p, t));
    if (ne) gen_pat_vars(w, p);
    return ZL_OK;
}

static int tc_program(mw_t *w)
{
    for (uint32_t i = 0; i < w->ntops; i++) {
        int32_t d = w->tops[i];
        if (N(w, d)->kind == N_COMMENT) continue;
        w->depth = 0;
        TRY(tc_decl(w, d));
        if (w->dial == D_HS)
            TRY(unify_at(w, d, N(w, d)->ty, TIO, "type error: main must be an IO action"));
    }
    return ZL_OK;
}

/* ------------------------------------------------------------ code */

static int E(mw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b) < 0 ? ZL_E_LIMIT : ZL_OK;
}

static int32_t here(mw_t *w)
{
    return (int32_t) w->c->p->ncode;
}

static int32_t jmp(mw_t *w, int op)
{
    int32_t at = zl_emit(w->c, op, 0, 0);
    return at < 0 ? ZL_E_LIMIT : at;
}

static void patch(mw_t *w, int32_t at)
{
    zl_patch(w->c, at, here(w));
}

static int fixup(mw_t *w, int32_t fn)
{
    if (w->nfix >= M_FIX) return zl_fail(w->c, ZL_E_LIMIT, "too many calls");
    int32_t at = zl_emit(w->c, ZL_CALL, 0, 0);
    if (at < 0) return ZL_E_LIMIT;
    w->fix[w->nfix].at = at;
    w->fix[w->nfix].fn = fn;
    w->nfix++;
    return ZL_OK;
}

static int helper(mw_t *w, int h)
{
    w->hused[h] = 1;
    return fixup(w, -h - 1);
}

/* a new local: a slot at the top level, a frame cell in a function */
static int32_t newloc(mw_t *w)
{
    if (w->lvl == 0) {
        int32_t s = zl_slots(w->c, 1);
        return s < 0 ? ZL_E_LIMIT : s;
    }
    mfn_t *f = &w->fns[w->curfn];
    if (f->np + f->nloc + 1 > ZL_MAX_FRAMES / 4) return zl_fail(w->c, ZL_E_LIMIT, "too many local values");
    return (int32_t) (f->np + f->nloc++);
}

static int ld(mw_t *w, int32_t addr, uint32_t level)
{
    if (level == 0) return E(w, ZL_LOAD, addr, 0);
    return E(w, ZL_LOADF, addr, (int32_t) (w->lvl - level));
}

static int st(mw_t *w, int32_t addr, uint32_t level)
{
    if (level == 0) return E(w, ZL_STORE, addr, 0);
    return E(w, ZL_STOREF, addr, (int32_t) (w->lvl - level));
}

/* after an integer operation: SML checks 32 bits, OCaml wraps 63 */
static int fixint(mw_t *w)
{
    if (w->dial == D_HS) return ZL_OK; /* the runtime traps outside the word */
    if (w->dial == D_SML) {
        if (w->c->p->word_bits == 62) return E(w, ZL_RANGE, 2147483647, 1);
        return ZL_OK;
    }
    if (w->c->p->word_bits == 64) return E(w, ZL_WRAP, 63, 0);
    return ZL_OK;
}

static int gen_exp(mw_t *w, int32_t e, int tail);
typedef struct {
    int32_t at[256];
    uint32_t n;
} mfails_t;
static int gen_rhs(mw_t *w, int32_t e, int tail, mfails_t *f);
static int gen_decl(mw_t *w, int32_t d);

/* evaluate the expressions of a list in the dialect's order; returns with
 * them on the stack, the first one deepest (SML) or on top (OCaml) */
static int gen_list_order(mw_t *w, int32_t first)
{
    int32_t items[M_GROUPS * M_GROUPS];
    uint32_t k = 0;
    for (int32_t q = first; q >= 0; q = N(w, q)->next) {
        if (k >= sizeof items / sizeof items[0]) return zl_fail(w->c, ZL_E_LIMIT, "too many arguments");
        items[k++] = q;
    }
    if (w->dial != D_OCAML)
        for (uint32_t i = 0; i < k; i++) TRY(gen_exp(w, items[i], 0));
    else
        for (uint32_t i = k; i-- > 0;) TRY(gen_exp(w, items[i], 0));
    return ZL_OK;
}

/* the flattened arguments of a call in evaluation order */
static int gen_args(mw_t *w, int32_t a0, uint32_t *n)
{
    int32_t items[M_GROUPS * M_GROUPS];
    uint32_t k = 0;
    for (int32_t q = a0; q >= 0; q = N(w, q)->next) {
        if (N(w, q)->kind == E_TUPLE) {
            for (int32_t x = N(w, q)->a; x >= 0; x = N(w, x)->next) items[k++] = x;
        } else {
            items[k++] = q;
        }
    }
    *n = k;
    if (w->dial != D_OCAML)
        for (uint32_t i = 0; i < k; i++) TRY(gen_exp(w, items[i], 0));
    else
        for (uint32_t i = k; i-- > 0;) TRY(gen_exp(w, items[i], 0));
    return ZL_OK;
}

static int is_simple(mw_t *w, int32_t e)
{
    int k = N(w, e)->kind;
    return k == E_INT || k == E_BOOL || k == E_UNIT || k == E_NIL || k == E_VAR;
}

/* ---- printing ---- */

typedef struct {
    int32_t node; /* E_STR literal, or the expression of an int/bool piece */
    uint8_t kind; /* 0 literal, 1 int, 2 bool, 3 format text */
    uint32_t at, len; /* format text: source span */
    int32_t tmp;
} mpiece_t;

static int pieces_of(mw_t *w, int32_t e, mpiece_t *ps, uint32_t *n)
{
    mnode_t *x = N(w, e);
    if (*n >= M_PIECES) return nfail(w, e, ZL_E_LIMIT, "too many pieces in one print");
    if (x->kind == E_STR) {
        ps[*n].node = e;
        ps[*n].kind = 0;
        ps[*n].at = (uint32_t) x->v + 1;
        ps[*n].len = x->len - 2u;
        (*n)++;
        return ZL_OK;
    }
    if (x->kind == E_BIN && x->op == O_CAT) {
        TRY(pieces_of(w, x->a, ps, n));
        return pieces_of(w, N(w, e)->b, ps, n);
    }
    /* toString */
    ps[*n].node = x->a;
    ps[*n].kind = (uint8_t) (-x->sym == B_ITOS ? 1 : 2);
    (*n)++;
    return ZL_OK;
}

/* the text of a literal piece, unescaped, emitted as OUTS */
static int out_text(mw_t *w, uint32_t at, uint32_t len)
{
    uint32_t k = 0;
    const char *s = w->src + at;
    for (uint32_t i = 0; i < len; i++) {
        char ch = s[i];
        if (ch == '\\') {
            char e = s[++i];
            ch = e == 'n' ? '\n' : e == 't' ? '\t' : e;
        }
        if (k >= M_STR) return zl_fail(w->c, ZL_E_LIMIT, "string too long");
        w->sbuf[k++] = ch;
    }
    if (!k) return ZL_OK;
    int32_t si = zl_addstr(w->c, w->sbuf, k);
    if (si < 0) return ZL_E_LIMIT;
    return E(w, ZL_OUTS, si, 0);
}

static int out_bool(mw_t *w)
{
    int32_t j = jmp(w, ZL_JZ);
    TRY(j);
    int hs = w->dial == D_HS;
    int32_t t = zl_addstr(w->c, hs ? "True" : "true", 4);
    if (t < 0) return ZL_E_LIMIT;
    TRY(E(w, ZL_OUTS, t, 0));
    int32_t k = jmp(w, ZL_JMP);
    TRY(k);
    patch(w, j);
    int32_t f = zl_addstr(w->c, hs ? "False" : "false", 5);
    if (f < 0) return ZL_E_LIMIT;
    TRY(E(w, ZL_OUTS, f, 0));
    patch(w, k);
    return ZL_OK;
}

/* print the value on top (popping it) as the piece says */
static int out_value(mw_t *w, int kind)
{
    if (kind == 2) return out_bool(w);
    if (w->dial == D_SML) return helper(w, H_SMLINT);
    return E(w, ZL_OUTI, 0, 0);
}

/* evaluate the pieces' values in the dialect's order, then print them all */
static int gen_print(mw_t *w, mpiece_t *ps, uint32_t n)
{
    uint32_t nv = 0;
    for (uint32_t i = 0; i < n; i++) nv += ps[i].kind == 1 || ps[i].kind == 2;
    if (w->dial == D_OCAML || nv <= 1) {
        for (uint32_t i = n; w->dial == D_OCAML && i-- > 0;)
            if (ps[i].kind == 1 || ps[i].kind == 2) TRY(gen_exp(w, ps[i].node, 0));
        for (uint32_t i = 0; w->dial == D_SML && i < n; i++)
            if (ps[i].kind == 1 || ps[i].kind == 2) TRY(gen_exp(w, ps[i].node, 0));
        for (uint32_t i = 0; i < n; i++) {
            if (ps[i].kind == 1 || ps[i].kind == 2)
                TRY(out_value(w, ps[i].kind));
            else
                TRY(out_text(w, ps[i].at, ps[i].len));
        }
        return ZL_OK;
    }
    /* SML, several values: left to right into locals, then print */
    for (uint32_t i = 0; i < n; i++) {
        if (ps[i].kind != 1 && ps[i].kind != 2) continue;
        TRY(gen_exp(w, ps[i].node, 0));
        ps[i].tmp = newloc(w);
        TRY(ps[i].tmp);
        TRY(st(w, ps[i].tmp, w->lvl));
    }
    for (uint32_t i = 0; i < n; i++) {
        if (ps[i].kind == 1 || ps[i].kind == 2) {
            TRY(ld(w, ps[i].tmp, w->lvl));
            TRY(out_value(w, ps[i].kind));
        } else {
            TRY(out_text(w, ps[i].at, ps[i].len));
        }
    }
    return ZL_OK;
}

static int gen_printf(mw_t *w, int32_t e)
{
    mpiece_t ps[M_PIECES];
    uint32_t n = 0;
    int32_t fmt = N(w, e)->a, arg = N(w, fmt)->next;
    const char *s = w->src + N(w, fmt)->v + 1;
    uint32_t len = N(w, fmt)->len - 2u, at = (uint32_t) N(w, fmt)->v + 1, i = 0, start = 0;
    for (; i <= len; i++) {
        if (i < len && s[i] == '\\') {
            i++;
            continue;
        }
        if (i < len && s[i] != '%') continue;
        if (i > start) {
            if (n >= M_PIECES) return nfail(w, e, ZL_E_LIMIT, "too many pieces in one print");
            ps[n].kind = 0;
            ps[n].at = at + start;
            ps[n].len = i - start;
            n++;
        }
        if (i == len) break;
        char f = s[i + 1];
        if (f == '%') {
            if (n >= M_PIECES) return nfail(w, e, ZL_E_LIMIT, "too many pieces in one print");
            ps[n].kind = 0;
            ps[n].at = at + i + 1;
            ps[n].len = 1;
            n++;
        } else if (f == 's') {
            TRY(pieces_of(w, arg, ps, &n));
            arg = N(w, arg)->next;
        } else {
            if (n >= M_PIECES) return nfail(w, e, ZL_E_LIMIT, "too many pieces in one print");
            ps[n].kind = (uint8_t) (f == 'b' ? 2 : 1);
            ps[n].node = arg;
            n++;
            arg = N(w, arg)->next;
        }
        i++;
        start = i + 1;
    }
    return gen_print(w, ps, n);
}

/* ---- patterns ---- */

typedef struct {
    int32_t addr;
    uint32_t level;
    uint8_t path[M_PATH]; /* 0 car, 1 cdr */
    uint32_t np;
} mloc_t;

static int load_loc(mw_t *w, const mloc_t *l)
{
    TRY(ld(w, l->addr, l->level));
    for (uint32_t i = 0; i < l->np; i++) TRY(E(w, l->path[i] ? ZL_CDR : ZL_CAR, 0, 0));
    return ZL_OK;
}

static int add_fail(mw_t *w, mfails_t *f)
{
    if (f->n >= 256) return zl_fail(w->c, ZL_E_LIMIT, "pattern too large");
    int32_t j = jmp(w, ZL_JZ);
    TRY(j);
    f->at[f->n++] = j;
    return ZL_OK;
}

static int gen_pat(mw_t *w, int32_t p, mloc_t *l, mfails_t *f)
{
    mnode_t *n = N(w, p);
    w->c->line = n->line;
    switch (n->kind) {
    case P_WILD:
    case P_UNIT:
        return ZL_OK;
    case P_VAR: {
        mbind_t *b = &w->binds[n->sym];
        if (l->np == 0) { /* the variable names the value where it is */
            b->addr = l->addr;
            b->level = (uint8_t) l->level;
            return ZL_OK;
        }
        int32_t a = newloc(w);
        TRY(a);
        b = &w->binds[n->sym];
        b->addr = a;
        b->level = (uint8_t) w->lvl;
        TRY(load_loc(w, l));
        return st(w, a, w->lvl);
    }
    case P_INT:
    case P_BOOL:
        if (!zl_fits(w->c, n->v))
            return nfail(w, p, ZL_E_UNSUPPORTED, "an integer outside the 62-bit words of a program with lists");
        TRY(load_loc(w, l));
        TRY(E(w, ZL_PUSH, n->v, 0));
        TRY(E(w, ZL_EQ, 0, 0));
        return add_fail(w, f);
    case P_NIL:
        TRY(load_loc(w, l));
        TRY(E(w, ZL_ISA, ZL_ISA_NULL, 0));
        return add_fail(w, f);
    case P_CONS: {
        if (l->np + 2 > M_PATH) return nfail(w, p, ZL_E_LIMIT, "pattern nested too deeply");
        TRY(load_loc(w, l));
        TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
        TRY(add_fail(w, f));
        l->path[l->np++] = 0;
        TRY(gen_pat(w, n->a, l, f));
        l->path[l->np - 1] = 1;
        TRY(gen_pat(w, N(w, p)->b, l, f));
        l->np--;
        return ZL_OK;
    }
    case P_LIST: {
        uint32_t base = l->np;
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            if (l->np + 2 > M_PATH) return nfail(w, p, ZL_E_LIMIT, "pattern nested too deeply");
            TRY(load_loc(w, l));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            TRY(add_fail(w, f));
            l->path[l->np++] = 0;
            TRY(gen_pat(w, q, l, f));
            l->path[l->np - 1] = 1;
        }
        TRY(load_loc(w, l));
        TRY(E(w, ZL_ISA, ZL_ISA_NULL, 0));
        TRY(add_fail(w, f));
        l->np = base;
        return ZL_OK;
    }
    default:
        break;
    }
    return nfail(w, p, ZL_E_SYNTAX, "expected a pattern");
}

/* a row pattern against k values at locs */
static int gen_row(mw_t *w, int32_t p, uint32_t k, const int32_t *addr, mfails_t *f)
{
    mloc_t l;
    l.level = w->lvl;
    l.np = 0;
    if (k == 1) {
        l.addr = addr[0];
        return gen_pat(w, p, &l, f);
    }
    if (N(w, p)->kind == P_WILD) return ZL_OK;
    uint32_t i = 0;
    for (int32_t q = N(w, p)->a; q >= 0; q = N(w, q)->next, i++) {
        l.addr = addr[i];
        l.np = 0;
        TRY(gen_pat(w, q, &l, f));
    }
    return ZL_OK;
}

static int trap_match(mw_t *w)
{
    return E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
}

/* rules: patterns against k values at addr; the result of the chosen body */
static int gen_rules(mw_t *w, int32_t r, uint32_t k, const int32_t *addr, int tail)
{
    int32_t ends[256];
    uint32_t ne = 0;
    int open = 1;
    for (; r >= 0; r = N(w, r)->next) {
        mfails_t f;
        f.n = 0;
        TRY(gen_row(w, N(w, r)->a, k, addr, &f));
        if (N(w, r)->b >= 0) {
            TRY(gen_exp(w, N(w, r)->b, 0));
            TRY(add_fail(w, &f));
        }
        TRY(gen_rhs(w, N(w, r)->c, tail, &f));
        if (N(w, r)->next >= 0 || f.n) {
            if (ne >= 256) return zl_fail(w->c, ZL_E_LIMIT, "too many rules");
            ends[ne] = jmp(w, ZL_JMP);
            TRY(ends[ne]);
            ne++;
        }
        for (uint32_t i = 0; i < f.n; i++) patch(w, f.at[i]);
        open = f.n > 0;
    }
    if (open) TRY(trap_match(w));
    for (uint32_t i = 0; i < ne; i++) patch(w, ends[i]);
    return ZL_OK;
}

/* ---- functions ---- */

/* the frame offset of flat parameter i */
static int32_t param_off(mw_t *w, const mfn_t *f, uint32_t i)
{
    return w->dial != D_OCAML ? (int32_t) i : (int32_t) (f->np - 1 - i);
}

static int gen_fun(mw_t *w, int32_t d)
{
    int32_t over = jmp(w, ZL_JMP);
    TRY(over);
    for (int32_t fb = N(w, d)->a; fb >= 0; fb = N(w, fb)->next) {
        int32_t fi = N(w, fb)->sym;
        int32_t savefn = w->curfn;
        uint32_t savelvl = w->lvl;
        w->curfn = fi;
        w->lvl = w->fns[fi].level;
        w->c->line = N(w, fb)->line;
        w->fns[fi].entry = here(w);
        w->fns[fi].enter_at = here(w);
        TRY(E(w, ZL_ENTER, 0, w->fns[fi].np));
        w->fns[fi].start = here(w);
        /* clauses: the parameter groups are the rows */
        int32_t ends[256];
        uint32_t ne = 0;
        int open = 1;
        for (int32_t c = N(w, fb)->a; c >= 0; c = N(w, c)->next) {
            mfails_t f;
            f.n = 0;
            uint32_t k = 0, g = 0;
            for (int32_t p = N(w, c)->a; p >= 0; p = N(w, p)->next, g++) {
                int32_t addr[M_GROUPS];
                uint32_t ar = w->fns[fi].gar[g];
                for (uint32_t i = 0; i < ar; i++) addr[i] = param_off(w, &w->fns[fi], k + i);
                TRY(gen_row(w, p, ar, addr, &f));
                k += ar;
            }
            if (N(w, c)->c >= 0) {
                TRY(gen_exp(w, N(w, c)->c, 0));
                TRY(add_fail(w, &f));
            }
            TRY(gen_rhs(w, N(w, c)->b, 1, &f));
            if (N(w, c)->next >= 0 || f.n) {
                if (ne >= 256) return zl_fail(w->c, ZL_E_LIMIT, "too many clauses");
                ends[ne] = jmp(w, ZL_JMP);
                TRY(ends[ne]);
                ne++;
            }
            for (uint32_t i = 0; i < f.n; i++) patch(w, f.at[i]);
            open = f.n > 0;
        }
        if (open) TRY(trap_match(w));
        for (uint32_t i = 0; i < ne; i++) patch(w, ends[i]);
        TRY(E(w, ZL_LEAVE, 0, 0));
        TRY(E(w, ZL_RET, 0, 0));
        mfn_t *f = &w->fns[fi];
        if (f->np + f->nloc > ZL_MAX_FRAMES) return zl_fail(w->c, ZL_E_LIMIT, "function frame too large");
        zl_patch(w->c, f->enter_at, (int64_t) (f->np + f->nloc));
        f->defined = 1;
        w->curfn = savefn;
        w->lvl = savelvl;
    }
    patch(w, over);
    return ZL_OK;
}

static int gen_call(mw_t *w, int32_t e, int tail)
{
    mnode_t *n = N(w, e);
    int32_t fi = w->binds[n->sym].fn;
    mfn_t *f = &w->fns[fi];
    uint32_t k;
    if (tail && fi == w->curfn && w->lvl == f->level) {
        TRY(gen_args(w, n->a, &k));
        w->c->line = N(w, e)->line;
        for (uint32_t i = k; i-- > 0;) TRY(E(w, ZL_STOREF, (int64_t) i, 0));
        return E(w, ZL_JMP, w->fns[fi].start, 0);
    }
    TRY(E(w, ZL_LINK, (int64_t) (w->lvl - (f->level - 1u)), 0));
    TRY(gen_args(w, n->a, &k));
    w->c->line = N(w, e)->line;
    return fixup(w, fi);
}

/* the type a node resolved to */
static int tykind(mw_t *w, int32_t t)
{
    return w->tys[tfind(w, t)].k;
}

/* show a value of the node's type (on top of the stack, popped) */
static int gen_show(mw_t *w, int32_t node)
{
    int32_t t = tfind(w, N(w, node)->ty);
    uint32_t depth = 0;
    while (w->tys[t].k == TY_LIST) {
        t = tfind(w, w->tys[t].a);
        depth++;
    }
    int k = w->tys[t].k;
    if (k == TY_VAR) {
        if (w->tys[t].gen)
            return nfail(w, node, ZL_E_UNSUPPORTED,
                         "show of a value whose type is a type variable (type classes) is not in this subset");
        return nfail(w, node, ZL_E_SYNTAX, "type error: the type of the value to show is ambiguous");
    }
    if (k == TY_IO) return nfail(w, node, ZL_E_SYNTAX, "type error: an IO action cannot be shown");
    if (depth == 0) {
        if (k == TY_INT) return E(w, ZL_OUTI, 0, 0);
        if (k == TY_BOOL) return out_bool(w);
        TRY(E(w, ZL_DROP, 0, 0));
        int32_t s = zl_addstr(w->c, "()", 2);
        if (s < 0) return ZL_E_LIMIT;
        return E(w, ZL_OUTS, s, 0);
    }
    if (k == TY_UNIT) return nfail(w, node, ZL_E_UNSUPPORTED, "show of a list of () is not in this subset");
    return helper(w, k == TY_BOOL ? H_SHOWLB : H_SHOWLI);
}

/* show the value of e, a tuple written out */
static int gen_shown(mw_t *w, int32_t e)
{
    if (N(w, e)->kind != E_TUPLE) {
        if (tykind(w, N(w, e)->ty) == TY_IO) return nfail(w, e, ZL_E_SYNTAX, "type error: an IO action cannot be shown");
        TRY(gen_exp(w, e, 0));
        return gen_show(w, e);
    }
    const char *sep = "(";
    for (int32_t q = N(w, e)->a; q >= 0; q = N(w, q)->next) {
        int32_t s = zl_addstr(w->c, sep, 1);
        if (s < 0) return ZL_E_LIMIT;
        TRY(E(w, ZL_OUTS, s, 0));
        TRY(gen_shown(w, q));
        sep = ",";
    }
    int32_t s = zl_addstr(w->c, ")", 1);
    if (s < 0) return ZL_E_LIMIT;
    return E(w, ZL_OUTS, s, 0);
}

/* print the pieces of a string expression: literals, show e, ++ */
static int gen_pieces(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    if (n->kind == E_STR) return out_text(w, (uint32_t) n->v + 1, n->len - 2u);
    if (n->kind == E_BIN) {
        TRY(gen_pieces(w, n->a));
        return gen_pieces(w, N(w, e)->b);
    }
    return gen_shown(w, n->a);
}

static int gen_hs_builtin(mw_t *w, int32_t e, int id)
{
    int32_t a0 = N(w, e)->a, b0 = a0 >= 0 ? N(w, a0)->next : -1;
    switch (id) {
    case B_HPRINT:
        TRY(gen_shown(w, a0));
        TRY(E(w, ZL_NL, 0, 0));
        return E(w, ZL_PUSH, 0, 0);
    case B_PRINT_STRING:
    case B_PRINT_ENDLINE:
        TRY(gen_pieces(w, a0));
        w->c->line = N(w, e)->line;
        if (id == B_PRINT_ENDLINE) TRY(E(w, ZL_NL, 0, 0));
        return E(w, ZL_PUSH, 0, 0);
    case B_RETURN:
        return gen_exp(w, a0, 0);
    default:
        break;
    }
    TRY(gen_exp(w, a0, 0));
    if (b0 >= 0) TRY(gen_exp(w, b0, 0));
    w->c->line = N(w, e)->line;
    switch (id) {
    case B_SIGNUM:
        return E(w, ZL_SGN, 0, 0);
    case B_NEGATE:
        return E(w, ZL_NEG, 0, 0);
    case B_EVEN:
    case B_ODD:
        TRY(E(w, ZL_PUSH, 2, 0));
        TRY(E(w, ZL_MODF, 0, 0));
        TRY(E(w, ZL_PUSH, 0, 0));
        return E(w, id == B_EVEN ? ZL_EQ : ZL_NE, 0, 0);
    case B_MIN:
    case B_MAX: {
        int tk = tykind(w, N(w, a0)->ty);
        if (!((tk == TY_LIST || tk == TY_VAR) && w->c->p->word_bits == 62))
            return E(w, id == B_MIN ? ZL_MIN : ZL_MAX, 0, 0);
        /* [x y]: min x y = if x <= y then x else y; max x y = if x <= y then y else x */
        TRY(E(w, ZL_OVER, 0, 0));
        TRY(E(w, ZL_OVER, 0, 0));
        TRY(helper(w, H_CMP));
        TRY(E(w, ZL_PUSH, 0, 0));
        TRY(E(w, ZL_GT, 0, 0));
        int32_t j = jmp(w, ZL_JZ);
        TRY(j);
        if (id == B_MIN) TRY(E(w, ZL_SWAP, 0, 0));
        TRY(E(w, ZL_DROP, 0, 0));
        int32_t k = jmp(w, ZL_JMP);
        TRY(k);
        patch(w, j);
        if (id == B_MAX) TRY(E(w, ZL_SWAP, 0, 0));
        TRY(E(w, ZL_DROP, 0, 0));
        patch(w, k);
        return ZL_OK;
    }
    case B_GCD:
        return helper(w, H_GCD);
    case B_LCM:
        return helper(w, H_LCM);
    case B_SUM:
        return helper(w, H_SUM);
    case B_PRODUCT:
        return helper(w, H_PRODUCT);
    case B_MAXIMUM:
        return helper(w, H_MAXIMUM);
    case B_MINIMUM:
        return helper(w, H_MINIMUM);
    case B_TAKE:
        return helper(w, H_TAKE);
    case B_DROP:
        return helper(w, H_DROP);
    case B_ELEM:
        return helper(w, H_ELEM);
    case B_NOTELEM:
        TRY(helper(w, H_ELEM));
        return E(w, ZL_NOT, 0, 0);
    case B_REPLICATE:
        return helper(w, H_REPLICATE);
    case B_LAST:
        return helper(w, H_LAST);
    case B_INIT:
        return helper(w, H_INIT);
    case B_CONCAT:
        return helper(w, H_CONCAT);
    case B_AND:
        return helper(w, H_AND);
    case B_OR:
        return helper(w, H_OR);
    default:
        break;
    }
    return nfail(w, e, ZL_E_UNSUPPORTED, "this library function is not in this subset");
}

static int gen_builtin(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    int id = -n->sym;
    int32_t a0 = n->a;
    if (w->dial == D_HS && (id >= B_HPRINT || id == B_PRINT_STRING || id == B_PRINT_ENDLINE ||
                            id == B_MIN || id == B_MAX))
        return gen_hs_builtin(w, e, id);
    switch (id) {
    case B_PRINT:
    case B_PRINT_STRING:
    case B_PRINT_ENDLINE: {
        mpiece_t ps[M_PIECES];
        uint32_t k = 0;
        TRY(pieces_of(w, a0, ps, &k));
        TRY(gen_print(w, ps, k));
        w->c->line = N(w, e)->line;
        if (id == B_PRINT_ENDLINE) TRY(E(w, ZL_NL, 0, 0));
        return E(w, ZL_PUSH, 0, 0);
    }
    case B_PRINTF:
        TRY(gen_printf(w, e));
        return E(w, ZL_PUSH, 0, 0);
    case B_PRINT_INT:
        TRY(gen_exp(w, a0, 0));
        TRY(E(w, ZL_OUTI, 0, 0));
        return E(w, ZL_PUSH, 0, 0);
    case B_PRINT_NEWLINE:
        TRY(gen_exp(w, a0, 0));
        TRY(E(w, ZL_DROP, 0, 0));
        TRY(E(w, ZL_NL, 0, 0));
        return E(w, ZL_PUSH, 0, 0);
    case B_ABS:
        TRY(gen_exp(w, a0, 0));
        TRY(E(w, ZL_ABS, 0, 0));
        return fixint(w);
    case B_SUCC:
    case B_PRED:
        TRY(gen_exp(w, a0, 0));
        TRY(E(w, ZL_PUSH, 1, 0));
        TRY(E(w, id == B_SUCC ? ZL_ADD : ZL_SUB, 0, 0));
        return fixint(w);
    case B_NOT:
        TRY(gen_exp(w, a0, 0));
        return E(w, ZL_NOT, 0, 0);
    case B_MIN:
    case B_MAX:
    case B_QUOT:
    case B_REM: {
        uint32_t k;
        int tk = N(w, a0)->ty >= 0 ? tykind(w, N(w, a0)->ty) : TY_INT;
        TRY(gen_args(w, a0, &k));
        if (w->dial == D_OCAML && (tk == TY_LIST || tk == TY_VAR) && w->c->p->word_bits == 62) {
            /* stack [b a]: OCaml's min is (if a <= b then a else b), max (if a >= b ...) */
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(helper(w, H_CMP)); /* compare b a */
            TRY(E(w, ZL_PUSH, 0, 0));
            TRY(E(w, id == B_MIN ? ZL_GE : ZL_LE, 0, 0));
            int32_t j = jmp(w, ZL_JZ);
            TRY(j);
            TRY(E(w, ZL_SWAP, 0, 0));
            patch(w, j);
            return E(w, ZL_DROP, 0, 0);
        }
        if (id == B_MIN || id == B_MAX) return E(w, id == B_MIN ? ZL_MIN : ZL_MAX, 0, 0);
        TRY(E(w, id == B_QUOT ? ZL_DIVT : ZL_MODT, 0, 0));
        return fixint(w);
    }
    case B_HD:
    case B_TL:
        TRY(gen_exp(w, a0, 0));
        return E(w, id == B_HD ? ZL_CAR : ZL_CDR, 0, 0);
    case B_NULL:
        TRY(gen_exp(w, a0, 0));
        return E(w, ZL_ISA, ZL_ISA_NULL, 0);
    case B_LENGTH:
        TRY(gen_exp(w, a0, 0));
        return helper(w, H_LEN);
    case B_REV:
        TRY(gen_exp(w, a0, 0));
        return helper(w, H_REV);
    default:
        break;
    }
    return nfail(w, e, ZL_E_UNSUPPORTED, "this library function is not in this subset");
}

/* both operands in the dialect's order; left on top only when swapped back */
static int gen_operands(mw_t *w, int32_t a, int32_t b, int commutes)
{
    if (w->dial != D_OCAML) {
        TRY(gen_exp(w, a, 0));
        return gen_exp(w, b, 0);
    }
    TRY(gen_exp(w, b, 0));
    TRY(gen_exp(w, a, 0));
    return commutes ? ZL_OK : E(w, ZL_SWAP, 0, 0);
}

static int gen_bin(mw_t *w, int32_t e)
{
    mnode_t *n = N(w, e);
    int op = n->op;
    int32_t a = n->a, b = n->b;
    switch (op) {
    case O_ANDALSO:
    case O_ORELSE: {
        TRY(gen_exp(w, a, 0));
        TRY(E(w, ZL_DUP, 0, 0));
        int32_t j = jmp(w, op == O_ANDALSO ? ZL_JZ : ZL_JNZ);
        TRY(j);
        TRY(E(w, ZL_DROP, 0, 0));
        TRY(gen_exp(w, b, 0));
        patch(w, j);
        return ZL_OK;
    }
    case O_ADD:
    case O_MUL:
        TRY(gen_operands(w, a, b, 1));
        w->c->line = N(w, e)->line;
        if (op == O_MUL && w->dial == D_OCAML && w->c->p->word_bits == 64) return helper(w, H_MUL63);
        TRY(E(w, op == O_ADD ? ZL_ADD : ZL_MUL, 0, 0));
        return fixint(w);
    case O_QUOT:
    case O_REM:
        TRY(gen_operands(w, a, b, 0));
        w->c->line = N(w, e)->line;
        return E(w, op == O_QUOT ? ZL_DIVT : ZL_MODT, 0, 0);
    case O_POW: { /* x ^ n: a negative exponent is an error in Haskell */
        TRY(gen_operands(w, a, b, 0));
        w->c->line = N(w, e)->line;
        TRY(E(w, ZL_DUP, 0, 0));
        TRY(E(w, ZL_PUSH, 0, 0));
        TRY(E(w, ZL_LT, 0, 0));
        int32_t j = jmp(w, ZL_JZ);
        TRY(j);
        TRY(E(w, ZL_TRAP, ZL_E_DOMAIN, 0));
        patch(w, j);
        return E(w, ZL_POW, 0, 0);
    }
    case O_INDEX:
        TRY(gen_operands(w, a, b, 0));
        w->c->line = N(w, e)->line;
        return helper(w, H_INDEX);
    case O_SUB:
    case O_DIV:
    case O_MOD:
        TRY(gen_operands(w, a, b, 0));
        w->c->line = N(w, e)->line;
        if (op == O_SUB)
            TRY(E(w, ZL_SUB, 0, 0));
        else if (w->dial != D_OCAML)
            TRY(E(w, op == O_DIV ? ZL_DIVF : ZL_MODF, 0, 0));
        else
            TRY(E(w, op == O_DIV ? ZL_DIVT : ZL_MODT, 0, 0));
        return fixint(w);
    case O_EQ:
    case O_NE: {
        int tk = tykind(w, N(w, a)->ty);
        TRY(gen_operands(w, a, b, 1));
        w->c->line = N(w, e)->line;
        if ((tk == TY_LIST || tk == TY_VAR) && w->c->p->word_bits == 62) {
            TRY(helper(w, H_EQ));
            return op == O_NE ? E(w, ZL_NOT, 0, 0) : ZL_OK;
        }
        return E(w, op == O_EQ ? ZL_EQ : ZL_NE, 0, 0);
    }
    case O_LT:
    case O_LE:
    case O_GT:
    case O_GE: {
        int tk = tykind(w, N(w, a)->ty);
        /* OCaml leaves the right operand deeper: flip the comparison */
        TRY(gen_operands(w, a, b, 1));
        w->c->line = N(w, e)->line;
        int zop = op == O_LT ? ZL_LT : op == O_LE ? ZL_LE : op == O_GT ? ZL_GT : ZL_GE;
        if (w->dial == D_OCAML)
            zop = op == O_LT ? ZL_GT : op == O_LE ? ZL_GE : op == O_GT ? ZL_LT : ZL_LE;
        if ((tk == TY_LIST || tk == TY_VAR) && w->c->p->word_bits == 62) {
            /* OCaml's structural order: numbers by value, [] first, lists by element */
            TRY(helper(w, H_CMP));
            TRY(E(w, ZL_PUSH, 0, 0));
        }
        return E(w, zop, 0, 0);
    }
    case O_CONS:
        TRY(gen_operands(w, a, b, 0));
        w->c->line = N(w, e)->line;
        return E(w, ZL_CONS, 0, 0);
    case O_APPEND:
        TRY(gen_operands(w, a, b, 0));
        w->c->line = N(w, e)->line;
        return helper(w, H_APPEND);
    default:
        break;
    }
    return nfail(w, e, ZL_E_SYNTAX, "unknown operator");
}

static int gen_exp(mw_t *w, int32_t e, int tail)
{
    mnode_t *n = N(w, e);
    w->c->line = n->line;
    if (w->dial == D_HS && n->ty >= 0 && tykind(w, n->ty) == TY_IO && !(n->flags & F_STMT))
        return nfail(w, e, ZL_E_UNSUPPORTED,
                     "an IO action used as a value is not in this subset (actions only run as statements)");
    switch (n->kind) {
    case E_RANGE: {
        uint32_t k = 0;
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next, k++) TRY(gen_exp(w, q, 0));
        w->c->line = N(w, e)->line;
        return helper(w, k == 2 ? H_RANGE : H_RANGE3);
    }
    case E_GUARDS: {
        mfails_t f;
        f.n = 0;
        TRY(gen_rhs(w, e, tail, &f));
        int32_t ok = jmp(w, ZL_JMP);
        TRY(ok);
        for (uint32_t i = 0; i < f.n; i++) patch(w, f.at[i]);
        TRY(trap_match(w));
        patch(w, ok);
        return ZL_OK;
    }
    case E_INT:
        if (!zl_fits(w->c, n->v))
            return nfail(w, e, ZL_E_UNSUPPORTED, "an integer outside the 62-bit words of a program with lists");
        return E(w, ZL_PUSH, n->v, 0);
    case E_BOOL:
        return E(w, ZL_PUSH, n->v, 0);
    case E_UNIT:
        return E(w, ZL_PUSH, 0, 0);
    case E_NIL:
        return E(w, ZL_PUSH, ZL_V_NIL, 0);
    case E_VAR: {
        if (n->sym < 0) {
            if (-n->sym == B_OTHERWISE) return E(w, ZL_PUSH, 1, 0);
            if (-n->sym == B_MAXINT) return E(w, ZL_PUSH, ((int64_t) 1 << 62) - 1, 0);
            if (w->c->p->word_bits == 62)
                return nfail(w, e, ZL_E_UNSUPPORTED, "min_int in a program with lists (62-bit words) is not in this subset");
            return E(w, ZL_PUSH, -((int64_t) 1 << 62), 0);
        }
        mbind_t *b = &w->binds[n->sym];
        if (b->kind == BK_LAZY) { /* force the thunk once, then read its value */
            uint32_t lb = b->level;
            int32_t addr = b->addr, fn = b->fn;
            TRY(ld(w, addr + 1, lb));
            int32_t j = jmp(w, ZL_JNZ);
            TRY(j);
            TRY(E(w, ZL_LINK, (int64_t) (w->lvl - lb), 0));
            TRY(fixup(w, fn));
            patch(w, j);
            return ld(w, addr, lb);
        }
        return ld(w, b->addr, b->level);
    }
    case E_LIST: {
        int simple = 1;
        uint32_t k = 0;
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next, k++) simple &= is_simple(w, q);
        if (simple || w->dial == D_OCAML) {
            /* build from the right: the last element first */
            int32_t items[256];
            uint32_t i = 0;
            if (k > 256) return nfail(w, e, ZL_E_LIMIT, "list literal too long");
            for (int32_t q = N(w, e)->a; q >= 0; q = N(w, q)->next) items[i++] = q;
            TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
            for (i = k; i-- > 0;) {
                TRY(gen_exp(w, items[i], 0));
                TRY(E(w, ZL_SWAP, 0, 0));
                TRY(E(w, ZL_CONS, 0, 0));
            }
            return ZL_OK;
        }
        if (k > ZL_MAX_STACK / 2) return nfail(w, e, ZL_E_LIMIT, "list literal too long");
        TRY(gen_list_order(w, N(w, e)->a));
        TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
        for (uint32_t i = 0; i < k; i++) TRY(E(w, ZL_CONS, 0, 0));
        return ZL_OK;
    }
    case E_BIN:
        return gen_bin(w, e);
    case E_NEG:
        TRY(gen_exp(w, n->a, 0));
        TRY(E(w, ZL_NEG, 0, 0));
        return fixint(w);
    case E_APP:
        if (n->sym < 0) return gen_builtin(w, e);
        return gen_call(w, e, tail);
    case E_IF: {
        TRY(gen_exp(w, n->a, 0));
        int32_t j = jmp(w, ZL_JZ);
        TRY(j);
        TRY(gen_exp(w, N(w, e)->b, tail));
        int32_t k = jmp(w, ZL_JMP);
        TRY(k);
        patch(w, j);
        if (N(w, e)->c >= 0)
            TRY(gen_exp(w, N(w, e)->c, tail));
        else
            TRY(E(w, ZL_PUSH, 0, 0));
        patch(w, k);
        return ZL_OK;
    }
    case E_CASE: {
        int32_t s = n->a, addr[M_GROUPS];
        uint32_t k = 1;
        if (N(w, s)->kind == E_TUPLE) {
            /* SML and OCaml both take a matched tuple apart left to right */
            k = 0;
            for (int32_t q = N(w, s)->a; q >= 0; q = N(w, q)->next) {
                TRY(gen_exp(w, q, 0));
                addr[k] = newloc(w);
                TRY(addr[k]);
                TRY(st(w, addr[k], w->lvl));
                k++;
            }
        } else {
            TRY(gen_exp(w, s, 0));
            addr[0] = newloc(w);
            TRY(addr[0]);
            TRY(st(w, addr[0], w->lvl));
        }
        return gen_rules(w, N(w, e)->b, k, addr, tail);
    }
    case E_LET:
        for (int32_t d = n->a; d >= 0; d = N(w, d)->next) TRY(gen_decl(w, d));
        return gen_exp(w, N(w, e)->b, tail);
    case E_SEQ:
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            TRY(gen_exp(w, q, N(w, q)->next < 0 ? tail : 0));
            if (N(w, q)->next >= 0) TRY(E(w, ZL_DROP, 0, 0));
        }
        return ZL_OK;
    default:
        break;
    }
    return nfail(w, e, ZL_E_SYNTAX, "expected an expression");
}

/* a Haskell value binding: its value and "computed" flag in two cells
 * here, and a thunk (a function of no parameters at the next level) that
 * computes the value into them; each use runs it once (gen_exp E_VAR) */
static int gen_lazy(mw_t *w, int32_t d)
{
    int32_t p = N(w, d)->a, e = N(w, d)->b, fi = N(w, d)->sym;
    uint32_t lb = w->lvl;
    int32_t a = newloc(w);
    TRY(a);
    int32_t fl = newloc(w);
    TRY(fl);
    if (fl != a + 1) return zl_fail(w->c, ZL_E_LIMIT, "no room for a value binding");
    mbind_t *b = &w->binds[N(w, p)->sym];
    b->addr = a;
    b->level = (uint8_t) lb;
    w->c->line = N(w, d)->line;
    TRY(E(w, ZL_PUSH, 0, 0));
    TRY(st(w, fl, lb));
    int32_t over = jmp(w, ZL_JMP);
    TRY(over);
    int32_t savefn = w->curfn;
    w->curfn = fi;
    w->lvl = w->fns[fi].level;
    w->fns[fi].entry = w->fns[fi].enter_at = here(w);
    TRY(E(w, ZL_ENTER, 0, 0));
    w->fns[fi].start = here(w);
    mfails_t f;
    f.n = 0;
    TRY(gen_rhs(w, e, 0, &f));
    if (f.n) {
        int32_t ok = jmp(w, ZL_JMP);
        TRY(ok);
        for (uint32_t i = 0; i < f.n; i++) patch(w, f.at[i]);
        TRY(trap_match(w));
        patch(w, ok);
    }
    w->c->line = N(w, d)->line;
    TRY(st(w, a, lb));
    TRY(E(w, ZL_PUSH, 1, 0));
    TRY(st(w, fl, lb));
    TRY(E(w, ZL_LEAVE, 0, 0));
    TRY(E(w, ZL_RET, 0, 0));
    mfn_t *fn = &w->fns[fi];
    if (fn->nloc > ZL_MAX_FRAMES) return zl_fail(w->c, ZL_E_LIMIT, "function frame too large");
    zl_patch(w->c, fn->enter_at, (int64_t) fn->nloc);
    fn->defined = 1;
    w->curfn = savefn;
    w->lvl = lb;
    patch(w, over);
    return ZL_OK;
}

/* a right side: where-declarations, then guards (falling through to f
 * when none holds) or an expression */
static int gen_rhs(mw_t *w, int32_t e, int tail, mfails_t *f)
{
    mnode_t *n = N(w, e);
    if (w->dial == D_HS && n->kind == E_LET && n->op == 1) {
        for (int32_t d = n->a; d >= 0; d = N(w, d)->next) TRY(gen_decl(w, d));
        return gen_rhs(w, N(w, e)->b, tail, f);
    }
    if (n->kind != E_GUARDS) return gen_exp(w, e, tail);
    w->c->line = n->line;
    if (n->ty >= 0 && tykind(w, n->ty) == TY_IO && !(n->flags & F_STMT))
        return nfail(w, e, ZL_E_UNSUPPORTED,
                     "an IO action used as a value is not in this subset (actions only run as statements)");
    int32_t ends[64];
    uint32_t ne = 0;
    for (int32_t g = n->a; g >= 0; g = N(w, g)->next) {
        int32_t c = N(w, g)->a, j = -1;
        int always = N(w, c)->kind == E_BOOL ? N(w, c)->v != 0
                                              : N(w, c)->kind == E_VAR && N(w, c)->sym == -B_OTHERWISE;
        if (!always) {
            TRY(gen_exp(w, c, 0));
            j = jmp(w, ZL_JZ);
            TRY(j);
        }
        TRY(gen_exp(w, N(w, g)->b, tail));
        if (ne >= 64) return nfail(w, g, ZL_E_LIMIT, "too many guards");
        ends[ne] = jmp(w, ZL_JMP);
        TRY(ends[ne]);
        ne++;
        if (j >= 0) patch(w, j);
    }
    if (f->n >= 256) return zl_fail(w->c, ZL_E_LIMIT, "pattern too large");
    f->at[f->n] = jmp(w, ZL_JMP);
    TRY(f->at[f->n]);
    f->n++;
    for (uint32_t i = 0; i < ne; i++) patch(w, ends[i]);
    return ZL_OK;
}

static int gen_decl(mw_t *w, int32_t d)
{
    mnode_t *n = N(w, d);
    w->c->line = n->line;
    if (n->kind == D_FUN) return gen_fun(w, d);
    if (n->kind == D_VAL && (n->flags & F_LAZY)) return gen_lazy(w, d);
    if (n->kind == D_EXP) {
        TRY(gen_exp(w, n->a, 0));
        return E(w, ZL_DROP, 0, 0);
    }
    int32_t p = n->a, e = n->b;
    if (N(w, e)->kind == E_TUPLE) {
        /* both dialects build a bound tuple right to left (OCaml) or left to right (SML) */
        int32_t items[M_GROUPS], addr[M_GROUPS];
        uint32_t k = 0;
        for (int32_t q = N(w, e)->a; q >= 0; q = N(w, q)->next) items[k++] = q;
        if (w->dial != D_OCAML) {
            for (uint32_t i = 0; i < k; i++) {
                TRY(gen_exp(w, items[i], 0));
                addr[i] = newloc(w);
                TRY(addr[i]);
                TRY(st(w, addr[i], w->lvl));
            }
        } else {
            for (uint32_t i = k; i-- > 0;) {
                TRY(gen_exp(w, items[i], 0));
                addr[i] = newloc(w);
                TRY(addr[i]);
                TRY(st(w, addr[i], w->lvl));
            }
        }
        mfails_t f;
        f.n = 0;
        TRY(gen_row(w, p, k, addr, &f));
        if (f.n) {
            int32_t ok = jmp(w, ZL_JMP);
            TRY(ok);
            for (uint32_t i = 0; i < f.n; i++) patch(w, f.at[i]);
            TRY(trap_match(w));
            patch(w, ok);
        }
        return ZL_OK;
    }
    TRY(gen_exp(w, e, 0));
    if (N(w, p)->kind == P_WILD || N(w, p)->kind == P_UNIT) return E(w, ZL_DROP, 0, 0);
    int32_t a = newloc(w);
    TRY(a);
    TRY(st(w, a, w->lvl));
    mfails_t f;
    f.n = 0;
    mloc_t l;
    l.addr = a;
    l.level = w->lvl;
    l.np = 0;
    TRY(gen_pat(w, p, &l, &f));
    if (f.n) {
        int32_t ok = jmp(w, ZL_JMP);
        TRY(ok);
        for (uint32_t i = 0; i < f.n; i++) patch(w, f.at[i]);
        TRY(trap_match(w));
        patch(w, ok);
    }
    return ZL_OK;
}

/* ---- helpers, emitted once after the program ---- */

/* emitters for the helper routines */
static int E0(mw_t *w, int op)
{
    return E(w, op, 0, 0);
}

/* [list?] traps DOMAIN unless the top is a pair (it stays) */
static int need_pair(mw_t *w)
{
    TRY(E0(w, ZL_DUP));
    TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
    int32_t j = jmp(w, ZL_JNZ);
    TRY(j);
    TRY(E(w, ZL_TRAP, ZL_E_DOMAIN, 0));
    patch(w, j);
    return ZL_OK;
}

/* [l] l a pair? leaves l; returns the JZ to patch at the loop's end */
static int32_t while_pair(mw_t *w)
{
    TRY(E0(w, ZL_DUP));
    TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
    return jmp(w, ZL_JZ);
}

/* scratch counter s: is it > 0? returns the JZ to patch */
static int32_t while_pos(mw_t *w, int32_t s)
{
    TRY(E(w, ZL_LOAD, s, 0));
    TRY(E(w, ZL_PUSH, 0, 0));
    TRY(E0(w, ZL_GT));
    return jmp(w, ZL_JZ);
}

static int dec_slot(mw_t *w, int32_t s)
{
    TRY(E(w, ZL_LOAD, s, 0));
    TRY(E(w, ZL_PUSH, 1, 0));
    TRY(E0(w, ZL_SUB));
    return E(w, ZL_STORE, s, 0);
}

/* the Haskell Prelude helpers; s0..s2 are scratch slots (no helper that
 * uses them runs another that does) */
static int gen_hs_helper(mw_t *w, int h)
{
    int32_t s0 = w->scratch, s1 = s0 + 1, s2 = s0 + 2, top = here(w), j, k, l;
    switch (h) {
    case H_SUM:
    case H_PRODUCT: /* [l] */
        TRY(E(w, ZL_PUSH, h == H_SUM ? 0 : 1, 0));
        TRY(E0(w, ZL_SWAP));
        l = here(w);
        j = while_pair(w);
        TRY(j);
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CAR));
        TRY(E0(w, ZL_ROT));
        TRY(E0(w, h == H_SUM ? ZL_ADD : ZL_MUL));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_CDR));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        return E0(w, ZL_RET);
    case H_MAXIMUM:
    case H_MINIMUM: /* [l], not empty */
        TRY(need_pair(w));
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CAR));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_CDR));
        l = here(w);
        j = while_pair(w);
        TRY(j);
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CAR));
        TRY(E0(w, ZL_ROT));
        TRY(E0(w, h == H_MAXIMUM ? ZL_MAX : ZL_MIN));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_CDR));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        return E0(w, ZL_RET);
    case H_TAKE: /* [n l] */
        TRY(E0(w, ZL_SWAP));
        TRY(E(w, ZL_STORE, s0, 0));
        TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
        TRY(E(w, ZL_STORE, s1, 0));
        l = here(w);
        j = while_pos(w, s0);
        TRY(j);
        k = while_pair(w);
        TRY(k);
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CAR));
        TRY(E(w, ZL_LOAD, s1, 0));
        TRY(E0(w, ZL_CONS));
        TRY(E(w, ZL_STORE, s1, 0));
        TRY(E0(w, ZL_CDR));
        TRY(dec_slot(w, s0));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        patch(w, k);
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_LOAD, s1, 0));
        TRY(helper(w, H_REV));
        return E0(w, ZL_RET);
    case H_DROP: /* [n l] */
        TRY(E0(w, ZL_SWAP));
        TRY(E(w, ZL_STORE, s0, 0));
        l = here(w);
        j = while_pos(w, s0);
        TRY(j);
        k = while_pair(w);
        TRY(k);
        TRY(E0(w, ZL_CDR));
        TRY(dec_slot(w, s0));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        patch(w, k);
        return E0(w, ZL_RET);
    case H_ELEM: { /* [x l] */
        l = here(w);
        j = while_pair(w);
        TRY(j);
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_CAR));
        TRY(helper(w, H_EQ));
        k = jmp(w, ZL_JNZ);
        TRY(k);
        TRY(E0(w, ZL_CDR));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_PUSH, 0, 0));
        TRY(E0(w, ZL_RET));
        patch(w, k);
        TRY(E0(w, ZL_DROP));
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_PUSH, 1, 0));
        return E0(w, ZL_RET);
    }
    case H_REPLICATE: /* [n x] */
        TRY(E0(w, ZL_SWAP));
        TRY(E(w, ZL_STORE, s0, 0));
        TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
        l = here(w);
        j = while_pos(w, s0);
        TRY(j);
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_CONS));
        TRY(dec_slot(w, s0));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_DROP));
        return E0(w, ZL_RET);
    case H_LAST: /* [l], not empty */
        TRY(need_pair(w));
        l = here(w);
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CDR));
        j = while_pair(w);
        TRY(j);
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        TRY(E0(w, ZL_CAR));
        return E0(w, ZL_RET);
    case H_INIT: /* [l], not empty */
        TRY(need_pair(w));
        TRY(helper(w, H_REV));
        TRY(E0(w, ZL_CDR));
        TRY(helper(w, H_REV));
        return E0(w, ZL_RET);
    case H_CONCAT: /* [ls] */
        TRY(helper(w, H_REV));
        TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
        TRY(E0(w, ZL_SWAP));
        l = here(w);
        j = while_pair(w);
        TRY(j);
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CAR));
        TRY(E0(w, ZL_ROT));
        TRY(helper(w, H_APPEND));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_CDR));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        return E0(w, ZL_RET);
    case H_AND:
    case H_OR: /* [l]: the first False (and) or True (or) decides */
        l = here(w);
        j = while_pair(w);
        TRY(j);
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CAR));
        k = jmp(w, h == H_AND ? ZL_JZ : ZL_JNZ);
        TRY(k);
        TRY(E0(w, ZL_CDR));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_PUSH, h == H_AND, 0));
        TRY(E0(w, ZL_RET));
        patch(w, k);
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_PUSH, h != H_AND, 0));
        return E0(w, ZL_RET);
    case H_RANGE: /* [a b]: [a .. b], built from b down */
        TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
        TRY(E(w, ZL_STORE, s1, 0));
        l = here(w);
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_GT));
        j = jmp(w, ZL_JNZ);
        TRY(j);
        TRY(E0(w, ZL_DUP));
        TRY(E(w, ZL_LOAD, s1, 0));
        TRY(E0(w, ZL_CONS));
        TRY(E(w, ZL_STORE, s1, 0));
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_EQ));
        k = jmp(w, ZL_JNZ);
        TRY(k);
        TRY(E(w, ZL_PUSH, 1, 0));
        TRY(E0(w, ZL_SUB));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        patch(w, k);
        TRY(E0(w, ZL_DROP));
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_LOAD, s1, 0));
        return E0(w, ZL_RET);
    case H_RANGE3: { /* [x1 x2 y]: [x1, x2 .. y]; a zero step that would never end traps */
        TRY(E(w, ZL_STORE, s2, 0));
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_SUB));
        TRY(E(w, ZL_STORE, s0, 0));
        TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
        TRY(E(w, ZL_STORE, s1, 0));
        int32_t fin[3], down;
        TRY(E(w, ZL_LOAD, s0, 0));
        TRY(E(w, ZL_PUSH, 0, 0));
        TRY(E0(w, ZL_LT));
        down = jmp(w, ZL_JNZ);
        TRY(down);
        TRY(E(w, ZL_LOAD, s0, 0));
        int32_t up = jmp(w, ZL_JNZ);
        TRY(up);
        TRY(E0(w, ZL_DUP)); /* step 0: [x1, x1 ..] has no end unless x1 > y */
        TRY(E(w, ZL_LOAD, s2, 0));
        TRY(E0(w, ZL_LE));
        fin[0] = jmp(w, ZL_JZ);
        TRY(fin[0]);
        TRY(E(w, ZL_TRAP, ZL_E_DOMAIN, 0));
        for (int dn = 0; dn < 2; dn++) {
            patch(w, dn ? down : up);
            l = here(w);
            TRY(E0(w, ZL_DUP));
            TRY(E(w, ZL_LOAD, s2, 0));
            TRY(E0(w, dn ? ZL_GE : ZL_LE));
            fin[1 + dn] = jmp(w, ZL_JZ);
            TRY(fin[1 + dn]);
            TRY(E0(w, ZL_DUP));
            TRY(E(w, ZL_LOAD, s1, 0));
            TRY(E0(w, ZL_CONS));
            TRY(E(w, ZL_STORE, s1, 0));
            TRY(E(w, ZL_LOAD, s0, 0));
            TRY(E0(w, ZL_ADD));
            TRY(E(w, ZL_JMP, l, 0));
        }
        for (int i = 0; i < 3; i++) patch(w, fin[i]);
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_LOAD, s1, 0));
        TRY(helper(w, H_REV));
        return E0(w, ZL_RET);
    }
    case H_INDEX: /* [l n]: l !! n */
        TRY(E0(w, ZL_DUP));
        TRY(E(w, ZL_PUSH, 0, 0));
        TRY(E0(w, ZL_LT));
        k = jmp(w, ZL_JNZ);
        TRY(k);
        l = here(w);
        TRY(E0(w, ZL_OVER));
        TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
        int32_t oob = jmp(w, ZL_JZ);
        TRY(oob);
        TRY(E0(w, ZL_DUP));
        j = jmp(w, ZL_JZ);
        TRY(j);
        TRY(E(w, ZL_PUSH, 1, 0));
        TRY(E0(w, ZL_SUB));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_CDR));
        TRY(E0(w, ZL_SWAP));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        TRY(E0(w, ZL_CAR));
        TRY(E0(w, ZL_RET));
        patch(w, k);
        patch(w, oob);
        return E(w, ZL_TRAP, ZL_E_BOUNDS, 0);
    case H_SHOWLI:
    case H_SHOWLB: { /* [l]: show l, nested lists by their own brackets */
        TRY(E(w, ZL_PUSH, '[', 0));
        TRY(E0(w, ZL_OUTC));
        j = while_pair(w);
        TRY(j);
        l = here(w);
        TRY(E0(w, ZL_DUP));
        TRY(E0(w, ZL_CAR));
        TRY(E0(w, ZL_DUP));
        TRY(E(w, ZL_ISA, ZL_ISA_NUMBER, 0));
        k = jmp(w, ZL_JZ);
        TRY(k);
        if (h == H_SHOWLI)
            TRY(E0(w, ZL_OUTI));
        else
            TRY(out_bool(w));
        int32_t nx = jmp(w, ZL_JMP);
        TRY(nx);
        patch(w, k);
        TRY(E(w, ZL_CALL, top, 0));
        patch(w, nx);
        TRY(E0(w, ZL_CDR));
        int32_t done = while_pair(w);
        TRY(done);
        TRY(E(w, ZL_PUSH, ',', 0));
        TRY(E0(w, ZL_OUTC));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        patch(w, done);
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_PUSH, ']', 0));
        TRY(E0(w, ZL_OUTC));
        return E0(w, ZL_RET);
    }
    case H_GCD: /* [a b] */
        TRY(E0(w, ZL_ABS));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_ABS));
        TRY(E0(w, ZL_SWAP));
        l = here(w);
        TRY(E0(w, ZL_DUP));
        j = jmp(w, ZL_JZ);
        TRY(j);
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_MODT));
        TRY(E(w, ZL_JMP, l, 0));
        patch(w, j);
        TRY(E0(w, ZL_DROP));
        return E0(w, ZL_RET);
    case H_LCM: /* [x y]: 0 if either is 0, else abs ((x `quot` gcd x y) * y) */
        TRY(E0(w, ZL_DUP));
        j = jmp(w, ZL_JZ);
        TRY(j);
        TRY(E0(w, ZL_OVER));
        k = jmp(w, ZL_JZ);
        TRY(k);
        TRY(E0(w, ZL_OVER));
        TRY(E0(w, ZL_OVER));
        TRY(helper(w, H_GCD));
        TRY(E0(w, ZL_ROT));
        TRY(E0(w, ZL_SWAP));
        TRY(E0(w, ZL_DIVT));
        TRY(E0(w, ZL_MUL));
        TRY(E0(w, ZL_ABS));
        TRY(E0(w, ZL_RET));
        patch(w, j);
        patch(w, k);
        TRY(E0(w, ZL_DROP));
        TRY(E0(w, ZL_DROP));
        TRY(E(w, ZL_PUSH, 0, 0));
        return E0(w, ZL_RET);
    default:
        break;
    }
    return ZL_OK;
}

static int gen_helpers(mw_t *w)
{
    w->c->line = 0;
    if (w->hused[H_TAKE] || w->hused[H_INIT] || w->hused[H_RANGE3]) w->hused[H_REV] = 1;
    if (w->hused[H_CONCAT]) w->hused[H_REV] = w->hused[H_APPEND] = 1;
    if (w->hused[H_ELEM]) w->hused[H_EQ] = 1;
    if (w->hused[H_LCM]) w->hused[H_GCD] = 1;
    if (w->hused[H_APPEND]) w->hused[H_REV] = w->hused[H_REVAPP] = 1;
    if (w->dial == D_HS && (w->hused[H_TAKE] || w->hused[H_DROP] || w->hused[H_REPLICATE] ||
                            w->hused[H_RANGE] || w->hused[H_RANGE3])) {
        w->scratch = zl_slots(w->c, 3);
        if (w->scratch < 0) return ZL_E_LIMIT;
    }
    if (w->hused[H_REV]) w->hused[H_REVAPP] = 1;
    for (int h = 0; h < H__COUNT; h++) {
        if (!w->hused[h]) continue;
        w->helper[h] = here(w);
        switch (h) {
        case H_SMLINT: { /* [x]: print x with ~ for the sign */
            TRY(E(w, ZL_DUP, 0, 0));
            TRY(E(w, ZL_PUSH, 0, 0));
            TRY(E(w, ZL_LT, 0, 0));
            int32_t pos = jmp(w, ZL_JZ);
            TRY(pos);
            TRY(E(w, ZL_PUSH, '~', 0));
            TRY(E(w, ZL_OUTC, 0, 0));
            TRY(E(w, ZL_DUP, 0, 0));
            TRY(E(w, ZL_PUSH, -2147483647 - 1, 0));
            TRY(E(w, ZL_EQ, 0, 0));
            int32_t neg = jmp(w, ZL_JZ);
            TRY(neg);
            TRY(E(w, ZL_DROP, 0, 0));
            int32_t s = zl_addstr(w->c, "2147483648", 10);
            if (s < 0) return ZL_E_LIMIT;
            TRY(E(w, ZL_OUTS, s, 0));
            TRY(E(w, ZL_RET, 0, 0));
            patch(w, neg);
            TRY(E(w, ZL_NEG, 0, 0));
            patch(w, pos);
            TRY(E(w, ZL_OUTI, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            break;
        }
        case H_MUL63: { /* [x y]: x * y modulo 2^63, as OCaml's int */
            int32_t s0 = w->scratch;
            const int64_t M = 0x7fffffff;
            TRY(E(w, ZL_STORE, s0 + 1, 0));
            TRY(E(w, ZL_STORE, s0, 0));
            TRY(E(w, ZL_LOAD, s0, 0));
            TRY(E(w, ZL_PUSH, M, 0));
            TRY(E(w, ZL_AND, 0, 0));
            TRY(E(w, ZL_STORE, s0 + 2, 0)); /* xl */
            TRY(E(w, ZL_LOAD, s0, 0));
            TRY(E(w, ZL_PUSH, 31, 0));
            TRY(E(w, ZL_SHR, 0, 0));
            TRY(E(w, ZL_STORE, s0, 0)); /* xh */
            TRY(E(w, ZL_LOAD, s0 + 1, 0));
            TRY(E(w, ZL_PUSH, M, 0));
            TRY(E(w, ZL_AND, 0, 0));
            TRY(E(w, ZL_STORE, s0 + 3, 0)); /* yl */
            TRY(E(w, ZL_LOAD, s0 + 1, 0));
            TRY(E(w, ZL_PUSH, 31, 0));
            TRY(E(w, ZL_SHR, 0, 0));
            TRY(E(w, ZL_STORE, s0 + 1, 0)); /* yh */
            TRY(E(w, ZL_LOAD, s0 + 2, 0));
            TRY(E(w, ZL_LOAD, s0 + 3, 0));
            TRY(E(w, ZL_MUL, 0, 0)); /* lo */
            TRY(E(w, ZL_LOAD, s0, 0));
            TRY(E(w, ZL_LOAD, s0 + 3, 0));
            TRY(E(w, ZL_MUL, 0, 0));
            TRY(E(w, ZL_LOAD, s0 + 2, 0));
            TRY(E(w, ZL_LOAD, s0 + 1, 0));
            TRY(E(w, ZL_MUL, 0, 0));
            TRY(E(w, ZL_ADD, 0, 0)); /* mid */
            TRY(E(w, ZL_WRAP, 32, 1));
            TRY(E(w, ZL_PUSH, 31, 0));
            TRY(E(w, ZL_SHL, 0, 0));
            TRY(E(w, ZL_WRAP, 63, 0));
            TRY(E(w, ZL_ADD, 0, 0));
            TRY(E(w, ZL_WRAP, 63, 0));
            TRY(E(w, ZL_LOAD, s0, 0));
            TRY(E(w, ZL_LOAD, s0 + 1, 0));
            TRY(E(w, ZL_MUL, 0, 0));
            TRY(E(w, ZL_PUSH, 1, 0));
            TRY(E(w, ZL_AND, 0, 0));
            TRY(E(w, ZL_PUSH, 62, 0));
            TRY(E(w, ZL_SHL, 0, 0));
            TRY(E(w, ZL_ADD, 0, 0));
            TRY(E(w, ZL_WRAP, 63, 0));
            TRY(E(w, ZL_RET, 0, 0));
            break;
        }
        case H_EQ: { /* [a b]: structural equality of numbers and lists */
            int32_t top = here(w);
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            int32_t apair = jmp(w, ZL_JNZ);
            TRY(apair);
            TRY(E(w, ZL_EQ, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            patch(w, apair);
            TRY(E(w, ZL_DUP, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            int32_t both = jmp(w, ZL_JNZ);
            TRY(both);
            int32_t ne = here(w);
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_PUSH, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            patch(w, both);
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_CAR, 0, 0));
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_CAR, 0, 0));
            TRY(E(w, ZL_CALL, top, 0));
            TRY(E(w, ZL_JZ, ne, 0));
            TRY(E(w, ZL_CDR, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_CDR, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_JMP, top, 0));
            break;
        }
        case H_CMP: { /* [a b]: -1, 0 or 1 as OCaml's compare orders them */
            int32_t top = here(w);
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            int32_t apair = jmp(w, ZL_JNZ);
            TRY(apair);
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_NULL, 0));
            int32_t anil = jmp(w, ZL_JNZ);
            TRY(anil);
            TRY(E(w, ZL_OVER, 0, 0)); /* numbers: (a > b) - (a < b) */
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_GT, 0, 0));
            TRY(E(w, ZL_ROT, 0, 0));
            TRY(E(w, ZL_ROT, 0, 0));
            TRY(E(w, ZL_LT, 0, 0));
            TRY(E(w, ZL_SUB, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            patch(w, anil); /* [] against a list: -1 or 0 */
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            TRY(E(w, ZL_NEG, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            patch(w, apair);
            TRY(E(w, ZL_DUP, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            int32_t both = jmp(w, ZL_JNZ);
            TRY(both);
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_PUSH, 1, 0));
            TRY(E(w, ZL_RET, 0, 0));
            patch(w, both);
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_CAR, 0, 0));
            TRY(E(w, ZL_OVER, 0, 0));
            TRY(E(w, ZL_CAR, 0, 0));
            TRY(E(w, ZL_CALL, top, 0));
            TRY(E(w, ZL_DUP, 0, 0));
            int32_t same = jmp(w, ZL_JZ);
            TRY(same);
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            patch(w, same);
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_CDR, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_CDR, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_JMP, top, 0));
            break;
        }
        case H_LEN: { /* [l]: its length */
            TRY(E(w, ZL_PUSH, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            int32_t loop = here(w);
            TRY(E(w, ZL_DUP, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            int32_t done = jmp(w, ZL_JZ);
            TRY(done);
            TRY(E(w, ZL_CDR, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_PUSH, 1, 0));
            TRY(E(w, ZL_ADD, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_JMP, loop, 0));
            patch(w, done);
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            break;
        }
        case H_REV: /* [l]: reversed */
            TRY(E(w, ZL_PUSH, ZL_V_NIL, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(helper(w, H_REVAPP));
            TRY(E(w, ZL_RET, 0, 0));
            break;
        case H_REVAPP: { /* [acc l]: l reversed onto acc */
            int32_t loop = here(w);
            TRY(E(w, ZL_DUP, 0, 0));
            TRY(E(w, ZL_ISA, ZL_ISA_PAIR, 0));
            int32_t done = jmp(w, ZL_JZ);
            TRY(done);
            TRY(E(w, ZL_DUP, 0, 0));
            TRY(E(w, ZL_CAR, 0, 0));
            TRY(E(w, ZL_ROT, 0, 0));
            TRY(E(w, ZL_CONS, 0, 0));
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(E(w, ZL_CDR, 0, 0));
            TRY(E(w, ZL_JMP, loop, 0));
            patch(w, done);
            TRY(E(w, ZL_DROP, 0, 0));
            TRY(E(w, ZL_RET, 0, 0));
            break;
        }
        case H_APPEND: /* [a b]: a followed by b */
            TRY(E(w, ZL_SWAP, 0, 0));
            TRY(helper(w, H_REV));
            TRY(helper(w, H_REVAPP));
            TRY(E(w, ZL_RET, 0, 0));
            break;
        default:
            TRY(gen_hs_helper(w, h));
            break;
        }
    }
    return ZL_OK;
}

static int gen_program(mw_t *w)
{
    w->curfn = -1;
    w->lvl = 0;
    w->c->p->word_bits = w->lists ? 62 : (w->dial == D_SML ? 32 : 64);
    if (w->dial == D_OCAML && !w->lists) {
        w->scratch = zl_slots(w->c, 4);
        if (w->scratch < 0) return ZL_E_LIMIT;
    }
    for (uint32_t i = 0; i < w->ntops; i++) {
        int32_t d = w->tops[i];
        if (N(w, d)->kind == N_COMMENT) continue;
        TRY(gen_decl(w, d));
    }
    TRY(E(w, ZL_HALT, 0, 0));
    TRY(gen_helpers(w));
    for (uint32_t i = 0; i < w->nfix; i++) {
        int32_t fn = w->fix[i].fn;
        int32_t to = fn < 0 ? w->helper[-fn - 1] : w->fns[fn].entry;
        zl_patch(w->c, w->fix[i].at, to);
    }
    return ZL_OK;
}

/* ------------------------------------------------------------ the twin */

static void tw(mw_t *w, const char *s)
{
    zl_puts(w->tw, s);
}

static void twn(mw_t *w, int32_t n)
{
    zl_put(w->tw, w->src + N(w, n)->v, N(w, n)->len);
}

static void twint(mw_t *w, int64_t v)
{
    if (v < 0 && w->dial == D_SML) {
        zl_putc(w->tw, '~');
        if (v == -2147483647 - 1) {
            tw(w, "2147483648");
            return;
        }
        zl_puti(w->tw, -v);
        return;
    }
    zl_puti(w->tw, v);
}

/* positions: how much of an open form may stand bare */
enum { PS_NONE = 0, PS_OPEN, PS_FULL };

/* expression precedence levels for the printer */
enum {
    L_TOP = 0, /* open forms */
    L_TUPLE,   /* OCaml a, b */
    L_OR,
    L_AND,
    L_CMP,
    L_CAT,  /* OCaml @ ^ */
    L_CONS, /* SML :: @ */
    L_ADD,
    L_MUL,
    L_NEG, /* OCaml unary minus */
    L_APP,
    L_ATOM
};

static int bin_level(mw_t *w, int op, int *right)
{
    *right = 0;
    switch (op) {
    case O_ORELSE:
        *right = w->dial == D_OCAML;
        return L_OR;
    case O_ANDALSO:
        *right = w->dial == D_OCAML;
        return L_AND;
    case O_EQ:
    case O_NE:
    case O_LT:
    case O_LE:
    case O_GT:
    case O_GE:
        return L_CMP;
    case O_CONS:
        *right = 1;
        return w->dial == D_SML ? L_CONS : L_CAT + 1;
    case O_APPEND:
        *right = 1;
        return w->dial == D_SML ? L_CONS : L_CAT;
    case O_CAT:
        *right = w->dial == D_OCAML;
        return w->dial == D_SML ? L_ADD : L_CAT;
    case O_ADD:
    case O_SUB:
        return L_ADD;
    default:
        return L_MUL;
    }
}

static const char *bin_text(mw_t *w, int op)
{
    switch (op) {
    case O_ADD:
        return "+";
    case O_SUB:
        return "-";
    case O_MUL:
        return "*";
    case O_DIV:
        return w->dial == D_SML ? "div" : "/";
    case O_MOD:
        return "mod";
    case O_EQ:
        return "=";
    case O_NE:
        return "<>";
    case O_LT:
        return "<";
    case O_LE:
        return "<=";
    case O_GT:
        return ">";
    case O_GE:
        return ">=";
    case O_ANDALSO:
        return w->dial == D_SML ? "andalso" : "&&";
    case O_ORELSE:
        return w->dial == D_SML ? "orelse" : "||";
    case O_CONS:
        return "::";
    case O_APPEND:
        return "@";
    default:
        return "^";
    }
}

static void pr_exp(mw_t *w, int32_t e, int lev, int pos);
static void pr_decl(mw_t *w, int32_t d);
static void pr_pat(mw_t *w, int32_t p, int lev);

/* patterns: lev 0 anywhere, 1 operand of ::, 2 atomic (a parameter) */
static void pr_pat(mw_t *w, int32_t p, int lev)
{
    mnode_t *n = N(w, p);
    switch (n->kind) {
    case P_WILD:
        tw(w, "_");
        return;
    case P_VAR:
        twn(w, p);
        return;
    case P_INT:
        if (n->v < 0 && w->dial == D_OCAML && lev >= 2) {
            tw(w, "(");
            twint(w, n->v);
            tw(w, ")");
            return;
        }
        twint(w, n->v);
        return;
    case P_BOOL:
        tw(w, n->v ? "true" : "false");
        return;
    case P_UNIT:
        tw(w, "()");
        return;
    case P_NIL:
        tw(w, "[]");
        return;
    case P_CONS:
        if (lev >= 1) tw(w, "(");
        pr_pat(w, n->a, 1);
        tw(w, " :: ");
        pr_pat(w, N(w, p)->b, 0);
        if (lev >= 1) tw(w, ")");
        return;
    case P_LIST:
        tw(w, "[");
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            pr_pat(w, q, 0);
            if (N(w, q)->next >= 0) tw(w, w->dial == D_SML ? ", " : "; ");
        }
        tw(w, "]");
        return;
    case P_TUPLE:
        tw(w, "(");
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            pr_pat(w, q, 0);
            if (N(w, q)->next >= 0) tw(w, ", ");
        }
        tw(w, ")");
        return;
    default:
        return;
    }
}

static int is_open(mw_t *w, int32_t e)
{
    int k = N(w, e)->kind;
    if (k == E_IF || k == E_CASE) return 1;
    if (w->dial == D_OCAML && (k == E_LET || k == E_SEQ)) return 1;
    return 0;
}

static void pr_list(mw_t *w, int32_t first, const char *sep, int lev, int pos)
{
    for (int32_t q = first; q >= 0; q = N(w, q)->next) {
        pr_exp(w, q, lev, pos);
        if (N(w, q)->next >= 0) tw(w, sep);
    }
}

static void pr_rules(mw_t *w, int32_t r, int pos)
{
    for (; r >= 0; r = N(w, r)->next) {
        int last = N(w, r)->next < 0;
        if (w->dial == D_OCAML)
            pr_pat(w, N(w, r)->a, 0);
        else
            pr_pat(w, N(w, r)->a, 0);
        if (N(w, r)->b >= 0) {
            tw(w, " when ");
            pr_exp(w, N(w, r)->b, L_TOP, PS_NONE);
        }
        tw(w, w->dial == D_SML ? " => " : " -> ");
        pr_exp(w, N(w, r)->c, L_TOP, last ? pos : PS_NONE);
        if (!last) tw(w, " | ");
    }
}

static void pr_exp(mw_t *w, int32_t e, int lev, int pos)
{
    mnode_t *n = N(w, e);
    int ocaml = w->dial == D_OCAML;
    if (is_open(w, e)) {
        int ok = lev <= L_TOP && (pos == PS_FULL || (pos == PS_OPEN && n->kind != E_SEQ));
        if (!ok) {
            tw(w, "(");
            pr_exp(w, e, L_TOP, PS_FULL);
            tw(w, ")");
            return;
        }
    }
    switch (n->kind) {
    case E_INT:
        if (ocaml && n->v < 0 && lev > L_NEG) {
            tw(w, "(");
            twint(w, n->v);
            tw(w, ")");
            return;
        }
        twint(w, n->v);
        return;
    case E_BOOL:
        tw(w, n->v ? "true" : "false");
        return;
    case E_UNIT:
        tw(w, "()");
        return;
    case E_NIL:
        tw(w, "[]");
        return;
    case E_VAR:
    case E_STR:
        twn(w, e);
        return;
    case E_LIST:
        tw(w, "[");
        pr_list(w, n->a, ocaml ? "; " : ", ", ocaml ? L_TUPLE + 1 : L_TOP, ocaml ? PS_NONE : PS_FULL);
        tw(w, "]");
        return;
    case E_TUPLE:
        tw(w, "(");
        pr_list(w, n->a, ", ", ocaml ? L_TUPLE + 1 : L_TOP, ocaml ? PS_NONE : PS_FULL);
        tw(w, ")");
        return;
    case E_BIN: {
        int right, bl = bin_level(w, n->op, &right);
        int paren = bl < lev;
        if (paren) tw(w, "(");
        pr_exp(w, n->a, right ? bl + 1 : bl, PS_NONE);
        tw(w, " ");
        tw(w, bin_text(w, n->op));
        tw(w, " ");
        pr_exp(w, N(w, e)->b, right ? bl : bl + 1, PS_NONE);
        if (paren) tw(w, ")");
        return;
    }
    case E_NEG: {
        /* a minus sign never touches another: ~(~5), -(-x) */
        int32_t a = n->a;
        int sign = N(w, a)->kind == E_NEG || (N(w, a)->kind == E_INT && N(w, a)->v < 0);
        int paren = lev > (ocaml ? L_NEG : L_APP);
        if (paren) tw(w, "(");
        tw(w, ocaml ? "-" : "~");
        if (sign) tw(w, "(");
        pr_exp(w, a, sign ? L_TOP : ocaml ? L_NEG : L_ATOM, sign ? PS_FULL : PS_NONE);
        if (sign) tw(w, ")");
        if (paren) tw(w, ")");
        return;
    }
    case E_APP: {
        int paren = lev > L_APP;
        if (paren) tw(w, "(");
        twn(w, e);
        for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
            tw(w, " ");
            pr_exp(w, q, L_ATOM, PS_NONE);
        }
        if (paren) tw(w, ")");
        return;
    }
    case E_IF:
        tw(w, "if ");
        pr_exp(w, n->a, L_TOP, PS_NONE);
        tw(w, " then ");
        pr_exp(w, N(w, e)->b, L_TOP, PS_NONE);
        if (N(w, e)->c >= 0) {
            tw(w, " else ");
            pr_exp(w, N(w, e)->c, L_TOP, PS_OPEN);
        }
        return;
    case E_CASE:
        tw(w, ocaml ? "match " : "case ");
        pr_exp(w, n->a, L_TOP, PS_NONE);
        tw(w, ocaml ? " with " : " of ");
        pr_rules(w, N(w, e)->b, ocaml ? PS_FULL : PS_FULL);
        return;
    case E_LET:
        if (ocaml) {
            pr_decl(w, n->a);
            tw(w, " in ");
            pr_exp(w, N(w, e)->b, L_TOP, PS_FULL);
            return;
        }
        tw(w, "let ");
        for (int32_t d = n->a; d >= 0; d = N(w, d)->next) {
            pr_decl(w, d);
            tw(w, " ");
        }
        tw(w, "in ");
        if (N(w, N(w, e)->b)->kind == E_SEQ)
            pr_list(w, N(w, N(w, e)->b)->a, "; ", L_TOP, PS_FULL);
        else
            pr_exp(w, N(w, e)->b, L_TOP, PS_FULL);
        tw(w, " end");
        return;
    case E_SEQ:
        if (ocaml) {
            for (int32_t q = n->a; q >= 0; q = N(w, q)->next) {
                int last = N(w, q)->next < 0;
                pr_exp(w, q, L_TOP, last ? pos : PS_NONE);
                if (!last) tw(w, "; ");
            }
            return;
        }
        tw(w, "(");
        pr_list(w, n->a, "; ", L_TOP, PS_FULL);
        tw(w, ")");
        return;
    default:
        return;
    }
}

static void pr_fb(mw_t *w, int32_t fb, int first, int rec, int top)
{
    if (w->dial == D_SML) {
        tw(w, first ? "fun " : (top ? "\nand " : " and "));
        for (int32_t c = N(w, fb)->a; c >= 0; c = N(w, c)->next) {
            int last = N(w, c)->next < 0;
            if (c != N(w, fb)->a) tw(w, top ? "\n  | " : " | ");
            twn(w, fb);
            for (int32_t p = N(w, c)->a; p >= 0; p = N(w, p)->next) {
                tw(w, " ");
                pr_pat(w, p, 2);
            }
            tw(w, " = ");
            pr_exp(w, N(w, c)->b, L_TOP, last ? PS_FULL : PS_NONE);
        }
        return;
    }
    tw(w, first ? (rec ? "let rec " : "let ") : (top ? "\nand " : " and "));
    twn(w, fb);
    if (N(w, fb)->flags & F_FUNCTION) {
        tw(w, " = function ");
        for (int32_t c = N(w, fb)->a; c >= 0; c = N(w, c)->next) {
            int last = N(w, c)->next < 0;
            pr_pat(w, N(w, c)->a, 0);
            if (N(w, c)->c >= 0) {
                tw(w, " when ");
                pr_exp(w, N(w, c)->c, L_TOP, PS_NONE);
            }
            tw(w, " -> ");
            pr_exp(w, N(w, c)->b, L_TOP, last ? PS_FULL : PS_NONE);
            if (!last) tw(w, " | ");
        }
        return;
    }
    int32_t c = N(w, fb)->a;
    for (int32_t p = N(w, c)->a; p >= 0; p = N(w, p)->next) {
        tw(w, " ");
        pr_pat(w, p, 2);
    }
    tw(w, " = ");
    pr_exp(w, N(w, c)->b, L_TOP, PS_FULL);
}

static void pr_decl_top(mw_t *w, int32_t d, int top)
{
    mnode_t *n = N(w, d);
    if (n->kind == D_FUN) {
        int first = 1;
        for (int32_t fb = n->a; fb >= 0; fb = N(w, fb)->next) {
            pr_fb(w, fb, first, (n->flags & F_REC) != 0, top);
            first = 0;
        }
        return;
    }
    if (n->kind == D_EXP) {
        if (w->dial == D_SML) {
            tw(w, "val _ = ");
        } else {
            tw(w, tykind(w, n->ty) == TY_UNIT ? "let () = " : "let _ = ");
        }
        pr_exp(w, n->a, L_TOP, PS_FULL);
        return;
    }
    tw(w, w->dial == D_SML ? "val " : "let ");
    if (w->dial == D_OCAML && N(w, n->a)->kind == P_TUPLE)
        pr_pat(w, n->a, 0);
    else
        pr_pat(w, n->a, 0);
    tw(w, " = ");
    if (N(w, n->b)->kind == E_TUPLE) {
        tw(w, "(");
        pr_list(w, N(w, n->b)->a, ", ", w->dial == D_OCAML ? L_TUPLE + 1 : L_TOP,
                w->dial == D_OCAML ? PS_NONE : PS_FULL);
        tw(w, ")");
        return;
    }
    pr_exp(w, n->b, L_TOP, PS_FULL);
}

static void pr_decl(mw_t *w, int32_t d)
{
    pr_decl_top(w, d, 0);
}

static void pr_program(mw_t *w)
{
    for (uint32_t i = 0; i < w->ntops; i++) {
        int32_t d = w->tops[i];
        if (N(w, d)->kind == N_COMMENT) {
            tw(w, "(*");
            if (N(w, d)->sym > 0) {
                tw(w, " ");
                zl_put(w->tw, w->src + N(w, d)->v, (size_t) N(w, d)->sym);
            }
            tw(w, " *)\n");
            continue;
        }
        pr_decl_top(w, d, 1);
        tw(w, "\n");
    }
}

/* ------------------------------------------------------------ entry */

void zl_ml_begin(mw_t *w, zl_cc_t *c, zl_out_t *tw_out, const char *src, size_t len, int dial)
{
    zl_bzero(w, offsetof(mw_t, nodes));
    w->c = c;
    w->tw = tw_out;
    w->src = src;
    w->len = len;
    w->dial = dial;
    w->lx.line = 1;
    w->tok.k = T_EOF;
    /* the shared type constants: TINT, TBOOL, TUNIT, TIO */
    for (int k = TY_INT; k <= TY_IO; k++) {
        if (k == TY_LIST) continue;
        w->tys[w->nty].k = (uint8_t) k;
        w->tys[w->nty].a = -1;
        w->nty++;
    }
    for (int h = 0; h < H__COUNT; h++) w->helper[h] = -1;
}

int32_t zl_ml_node(mw_t *w, int kind, uint32_t line, uint32_t col)
{
    mtok_t t;
    t.line = line;
    t.col = col;
    return newnode(w, kind, &t);
}

int zl_ml_fail(mw_t *w, int st, uint32_t line, uint32_t col, const char *msg)
{
    return mfail(w, st, line, col, msg);
}

int zl_ml_finish(mw_t *w)
{
    TRY(tc_program(w));
    return gen_program(w);
}

int zl_ml_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw_out, void *work)
{
    mw_t *w = (mw_t *) work;
    zl_ml_begin(w, c, tw_out, src, len, c->p->lang == ZL_LANG_OCAML ? D_OCAML : D_SML);
    TRY(p_program(w));
    TRY(zl_ml_finish(w));
    if (c->failed) return c->d->status;
    if (tw_out) pr_program(w);
    return ZL_OK;
}
