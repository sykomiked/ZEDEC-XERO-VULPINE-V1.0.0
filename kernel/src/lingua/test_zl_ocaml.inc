/* test_zl_ocaml.inc — OCaml (twin Variantwyrm) front-end tests, included by
 * test_zl.c. The front-end is zl_ml.c, shared with ML. Generated from a case
 * list in which every output was checked against the ocaml 4.14.1 toplevel,
 * every trap against an uncaught OCaml exception at the same point of the
 * output, and every SYNTAX or UNDEFINED refusal against an OCaml compile
 * error. UNSUPPORTED refusals are valid OCaml outside the subset. */

static void test_ocaml(void)
{
    const int L = ZL_LANG_OCAML;

    /* outputs, each checked against the reference */
    expect_out(L,
        "let () = print_string \"hello\\n\"\n",
        "hello\n",
        "print_string");
    expect_out(L,
        "let () = print_string \"a\\tb\\\\c\\\"d\\n\"\n",
        "a\tb\\c\"d\n",
        "string escapes");
    expect_out(L,
        "let () = print_int (42); print_newline ()\n"
        "let () = print_int (-7); print_newline ()\n",
        "42\n"
        "-7\n",
        "print_int");
    expect_out(L,
        "let () = print_endline \"x\"; print_newline (); print_int 1\n",
        "x\n"
        "\n"
        "1",
        "print_endline and print_newline");
    expect_out(L,
        "let () = Printf.printf \"%d|%i|%b|%s|%%\\n\" 1 (-2) true (\"s\" ^ string_of_int 3)\n",
        "1|-2|true|s3|%\n",
        "Printf.printf");
    expect_out(L,
        "let () = print_int (1 + 2 * 3 - 4); print_newline ()\n"
        "let () = print_int ((1 + 2) * 3); print_newline ()\n"
        "let () = print_int (10 - 3 - 2); print_newline ()\n",
        "3\n"
        "9\n"
        "5\n",
        "precedence and associativity");
    expect_out(L,
        "let () = print_int (7 / 2); print_newline ()\n"
        "let () = print_int (-7 / 2); print_newline ()\n"
        "let () = print_int (7 / -2); print_newline ()\n"
        "let () = print_int (-7 / -2); print_newline ()\n",
        "3\n"
        "-3\n"
        "-3\n"
        "3\n",
        "/ truncates");
    expect_out(L,
        "let () = print_int (7 mod 2); print_newline ()\n"
        "let () = print_int (-7 mod 2); print_newline ()\n"
        "let () = print_int (7 mod -2); print_newline ()\n"
        "let () = print_int (-7 mod -2); print_newline ()\n",
        "1\n"
        "-1\n"
        "1\n"
        "-1\n",
        "mod takes the sign of the dividend");
    expect_out(L,
        "let () = print_int (abs (-3)); print_newline ()\n"
        "let () = print_int (min 2 (-5)); print_newline ()\n"
        "let () = print_int (max 2 (-5)); print_newline ()\n"
        "let () = print_int (succ 4); print_newline ()\n"
        "let () = print_int (pred 4); print_newline ()\n",
        "3\n"
        "-5\n"
        "2\n"
        "5\n"
        "3\n",
        "abs min max succ pred");
    expect_out(L,
        "let x = 5\n"
        "let () = print_int (-x); print_newline ()\n"
        "let () = print_int (- (2 + 3)); print_newline ()\n"
        "let () = print_int (- 5); print_newline ()\n"
        "let () = print_int (-(-x)); print_newline ()\n",
        "-5\n"
        "-5\n"
        "-5\n"
        "5\n",
        "negation");
    expect_out(L,
        "let () = print_int (max_int); print_newline ()\n"
        "let () = print_int (min_int); print_newline ()\n",
        "4611686018427387903\n"
        "-4611686018427387904\n",
        "max_int and min_int");
    expect_out(L,
        "let () = print_int (max_int + 1); print_newline ()\n"
        "let () = print_int (min_int - 1); print_newline ()\n"
        "let () = print_int (- min_int); print_newline ()\n"
        "let () = print_int (abs min_int); print_newline ()\n"
        "let () = print_int (min_int / -1); print_newline ()\n",
        "-4611686018427387904\n"
        "4611686018427387903\n"
        "-4611686018427387904\n"
        "-4611686018427387904\n"
        "-4611686018427387904\n",
        "addition, negation and division wrap");
    expect_out(L,
        "let () = print_int (max_int * max_int); print_newline ()\n"
        "let () = print_int (max_int * 3); print_newline ()\n"
        "let () = print_int (123456789123 * 987654321987); print_newline ()\n"
        "let () = print_int (-123456789123 * 987654321987); print_newline ()\n"
        "let () = print_int (min_int * -1); print_newline ()\n"
        "let () = print_int (3037000499 * 3037000499 * 7); print_newline ()\n",
        "1\n"
        "4611686018427387901\n"
        "-346971251534834359\n"
        "346971251534834359\n"
        "-4611686018427387904\n"
        "-41499687649\n",
        "multiplication wraps modulo 2^63");
    expect_out(L,
        "let () = print_endline (string_of_bool (1 < 2))\n"
        "let () = print_endline (string_of_bool (2 <= 1))\n"
        "let () = print_endline (string_of_bool (3 > 2))\n"
        "let () = print_endline (string_of_bool (3 >= 4))\n"
        "let () = print_endline (string_of_bool (1 = 1))\n"
        "let () = print_endline (string_of_bool (1 <> 1))\n",
        "true\n"
        "false\n"
        "true\n"
        "false\n"
        "true\n"
        "false\n",
        "comparisons");
    expect_out(L,
        "let () = print_endline (string_of_bool (false && 1 / 0 = 1))\n"
        "let () = print_endline (string_of_bool (true || 1 / 0 = 1))\n"
        "let () = print_endline (string_of_bool (not (1 = 2)))\n"
        "let () = print_endline (string_of_bool (false < true))\n"
        "let () = print_endline (string_of_bool (max false true))\n",
        "false\n"
        "true\n"
        "true\n"
        "true\n"
        "true\n",
        "booleans");
    expect_out(L,
        "let () = print_int (if 1 < 2 then 10 else 20); print_newline ()\n"
        "let () = if 1 > 2 then print_string \"no\"; print_string \"yes\\n\"\n",
        "10\n"
        "yes\n",
        "if, and if without else");
    expect_out(L,
        "let f n = match n with 0 -> 100 | -1 -> 200 | _ -> n\n"
        "let () = print_int (f 0); print_newline ()\n"
        "let () = print_int (f (-1)); print_newline ()\n"
        "let () = print_int (f 9); print_newline ()\n",
        "100\n"
        "200\n"
        "9\n",
        "match on integers");
    expect_out(L,
        "let f = function true -> 1 | false -> 0\n"
        "let () = print_int (f true + f false); print_newline ()\n",
        "1\n",
        "function on booleans");
    expect_out(L,
        "let f n = match n with x when x < 0 -> -1 | 0 -> 0 | _ -> 1\n"
        "let () = print_int (f (-5) + 10 * f 0 + 100 * f 7); print_newline ()\n",
        "99\n",
        "guards");
    expect_out(L,
        "let x = let a = 2 in let b = a * 3 in a + b\n"
        "let () = print_int (x); print_newline ()\n",
        "8\n",
        "let in");
    expect_out(L,
        "let rec fact n = if n = 0 then 1 else n * fact (n - 1)\n"
        "let () = print_int (fact 20); print_newline ()\n",
        "2432902008176640000\n",
        "recursion");
    expect_out(L,
        "let add a b c = a + b * c\n"
        "let () = print_int (add 1 2 3); print_newline ()\n",
        "7\n",
        "curried parameters");
    expect_out(L,
        "let rec gcd (a, b) = if b = 0 then a else gcd (b, a mod b)\n"
        "let () = print_int (gcd (1071, 462)); print_newline ()\n",
        "21\n",
        "tupled parameters");
    expect_out(L,
        "let f n = let g x = x + n in let h y = g y * 2 in h 1\n"
        "let () = print_int (f 10); print_newline ()\n",
        "22\n",
        "nested functions read their parents");
    expect_out(L,
        "let rec even n = if n = 0 then true else odd (n - 1)\n"
        "and odd n = if n = 0 then false else even (n - 1)\n"
        "let () = print_endline (string_of_bool (even 10))\n"
        "let () = print_endline (string_of_bool (odd 10))\n",
        "true\n"
        "false\n",
        "mutual recursion");
    expect_out(L,
        "let rec loop n s = if n = 0 then s else loop (n - 1) (s + 1)\n"
        "let () = print_int (loop 100000 0); print_newline ()\n",
        "100000\n",
        "a tail call is a loop");
    expect_out(L,
        "let (a, b) = (3, 4)\n"
        "let c, d = 5, 6\n"
        "let () = print_int (a * b + c * d); print_newline ()\n",
        "42\n",
        "tuple bindings");
    expect_out(L,
        "let x = 1\n"
        "let x = x + 1\n"
        "let () = print_int (x); print_newline ()\n",
        "2\n",
        "shadowing");
    expect_out(L,
        "let f x = x + 1\n"
        "let f x = f (f x)\n"
        "let () = print_int (f 1); print_newline ()\n",
        "3\n",
        "let without rec sees the old name");
    expect_out(L,
        "let f x = print_int x; x\n"
        "let () = print_int (f 1 + f 2); print_newline ()\n"
        "let () = print_string (string_of_int (f 3) ^ string_of_int (f 4) ^ \"\\n\")\n"
        "let g a b = a - b\n"
        "let () = print_int (g (f 5) (f 6)); print_newline ()\n",
        "213\n"
        "4334\n"
        "65-1\n",
        "right-to-left evaluation");
    expect_out(L,
        "let f x = print_int x; x\n"
        "let () = match (f 1, f 2) with (a, b) -> print_int (a + b)\n"
        "let () = let (a, b) = (f 3, f 4) in print_int (a + b); print_newline ()\n",
        "123437\n",
        "a matched tuple left to right, a bound one right to left");
    expect_out(L,
        ";;\n"
        "print_string \"a\";;\n"
        "let () = print_string \"b\\n\";;\n"
        "print_string \"c\\n\"\n",
        "ab\n"
        "c\n",
        "top-level expressions");
    expect_out(L,
        "(* a (* nested *) comment *)\n"
        "let () = print_string \"x\\n\" (* after *)\n",
        "x\n",
        "comments");
    expect_out(L,
        "let l = [1; 2; 3;]\n"
        "let () = print_int (List.hd l); print_newline ()\n"
        "let () = print_int (List.hd (List.tl l)); print_newline ()\n"
        "let () = print_int (List.length l); print_newline ()\n",
        "1\n"
        "2\n"
        "3\n",
        "list basics");
    expect_out(L,
        "let rec sum = function [] -> 0 | x :: xs -> x + sum xs\n"
        "let () = print_int (sum [1; 2; 3; 4]); print_newline ()\n"
        "let () = print_int (sum (List.rev [5; 6] @ [7])); print_newline ()\n",
        "10\n"
        "18\n",
        "list recursion");
    expect_out(L,
        "let f = function [] -> 0 | [x] -> x | [x; y] -> x * y | x :: y :: _ -> x + y\n"
        "let () = print_int (f []); print_newline ()\n"
        "let () = print_int (f [7]); print_newline ()\n"
        "let () = print_int (f [3; 4]); print_newline ()\n"
        "let () = print_int (f [3; 4; 5]); print_newline ()\n",
        "0\n"
        "7\n"
        "12\n"
        "7\n",
        "list patterns");
    expect_out(L,
        "let () = print_endline (string_of_bool ([1; 2] = [1; 2]))\n"
        "let () = print_endline (string_of_bool ([1; 2] = [1]))\n"
        "let () = print_endline (string_of_bool ([[1]; []] <> [[1]; []]))\n"
        "let () = print_endline (string_of_bool ([true] = [true]))\n",
        "true\n"
        "false\n"
        "false\n"
        "true\n",
        "structural equality");
    expect_out(L,
        "let () = print_endline (string_of_bool ([1; 2] < [1; 3]))\n"
        "let () = print_endline (string_of_bool ([] < [0]))\n"
        "let () = print_endline (string_of_bool ([2] > [1; 5]))\n"
        "let () = print_endline (string_of_bool ([[1]] <= [[1]]))\n"
        "let () = print_int (List.length (max [1] [1; 0])); print_newline ()\n",
        "true\n"
        "true\n"
        "true\n"
        "true\n"
        "2\n",
        "structural order");
    expect_out(L,
        "let rec len = function [] -> 0 | _ :: t -> 1 + len t\n"
        "let () = print_int (len [1; 2] + len [true]); print_newline ()\n",
        "3\n",
        "a polymorphic function");
    expect_out(L,
        "let e = []\n"
        "let a = 1 :: e\n"
        "let b = true :: e\n"
        "let () = print_int (List.length a + List.length b); print_newline ()\n",
        "2\n",
        "a polymorphic value");
    expect_out(L,
        "let lt a b = a < b\n"
        "let () = print_endline (string_of_bool (lt 1 2))\n"
        "let () = print_endline (string_of_bool (lt [2] [1]))\n",
        "true\n"
        "false\n",
        "polymorphic comparison");
    expect_out(L,
        "let () = (print_string \"x\"; print_string \"y\\n\")\n"
        "let () = begin print_string \"z\"; print_newline () end\n",
        "xy\n"
        "z\n",
        "sequences");
    expect_out(L,
        "let f () = 5\n"
        "let () = print_int (f ()); print_newline ()\n",
        "5\n",
        "a unit parameter");
    expect_out(L,
        "let _ = 1 + 1\n"
        "let _ = print_string \"w\\n\"\n",
        "w\n",
        "let _");
    expect_out(L,
        "let () = print_int 4611686018427387904\n",
        "-4611686018427387904",
        "the literal 2^62 is min_int");
    expect_out(L,
        "let () = Printf.printf \"%b %b\\n\" (1 :: [2] = [1; 2]) ([1] @ [2] = 1 :: [3])\n",
        "true false\n",
        "an operator after :: and @");

    /* run-time traps: the reference stops with an uncaught exception */
    expect_trap(L,
        "let () = print_int (1 / 0); print_newline ()\n",
        ZL_E_DIVZERO, 1, "division by zero");
    expect_trap(L,
        "let () = print_string \"a\"\n"
        "let () = print_int (1 mod 0); print_newline ()\n",
        ZL_E_DIVZERO, 2, "mod by zero");
    expect_trap(L,
        "let f = function 0 -> 1\n"
        "let () = print_int (f 2); print_newline ()\n",
        ZL_E_DOMAIN, 1, "a match that fails");
    expect_trap(L,
        "let () = print_int (List.hd []); print_newline ()\n",
        ZL_E_DOMAIN, 1, "List.hd of the empty list");
    expect_trap(L,
        "let [a] = [1; 2]\n",
        ZL_E_DOMAIN, 1, "a binding that fails");

    /* refusals, with the line they are reported on */
    expect_refused(L,
        "let x = 1.5\n",
        ZL_E_UNSUPPORTED, 1, "floats");
    expect_refused(L,
        "let x = 0x1F\n",
        ZL_E_UNSUPPORTED, 1, "hexadecimal");
    expect_refused(L,
        "let c = 'a'\n",
        ZL_E_UNSUPPORTED, 1, "characters");
    expect_refused(L,
        "let s = \"abc\"\n",
        ZL_E_UNSUPPORTED, 1, "a string value");
    expect_refused(L,
        "let f = fun x -> x\n",
        ZL_E_UNSUPPORTED, 1, "fun");
    expect_refused(L,
        "let f x = x\n"
        "let g = f\n",
        ZL_E_UNSUPPORTED, 2, "a function as a value");
    expect_refused(L,
        "let f a b = a\n"
        "let g = f 1\n",
        ZL_E_UNSUPPORTED, 2, "partial application");
    expect_refused(L,
        "type t = A | B\n",
        ZL_E_UNSUPPORTED, 1, "type");
    expect_refused(L,
        "exception E\n",
        ZL_E_UNSUPPORTED, 1, "exceptions");
    expect_refused(L,
        "let r = ref 0\n",
        ZL_E_UNSUPPORTED, 1, "references");
    expect_refused(L,
        "let x = (1, 2)\n",
        ZL_E_UNSUPPORTED, 1, "a tuple value");
    expect_refused(L,
        "let x = Some 1\n",
        ZL_E_UNSUPPORTED, 1, "constructors");
    expect_refused(L,
        "let () = for i = 1 to 3 do print_int i done\n",
        ZL_E_UNSUPPORTED, 1, "for");
    expect_refused(L,
        "let x : int = 1\n",
        ZL_E_UNSUPPORTED, 1, "type annotations");
    expect_refused(L,
        "let x = List.map\n",
        ZL_E_UNSUPPORTED, 1, "List.map");
    expect_refused(L,
        "let x = [1] == [1]\n",
        ZL_E_UNSUPPORTED, 1, "physical equality");
    expect_refused(L,
        "let f = function 0 | 1 -> 1 | _ -> 2\n",
        ZL_E_UNSUPPORTED, 1, "or-patterns");
    expect_refused(L,
        "let l = [1]\n"
        "let x = min_int\n",
        ZL_E_UNSUPPORTED, 2, "min_int in a list program");
    expect_refused(L,
        "let x = y\n",
        ZL_E_UNDEFINED, 1, "an unbound name");
    expect_refused(L,
        "let x = 1 + true\n",
        ZL_E_SYNTAX, 1, "a type error");
    expect_refused(L,
        "let x = if 1 then 2 else 3\n",
        ZL_E_SYNTAX, 1, "a condition that is not boolean");
    expect_refused(L,
        "let x = [1; true]\n",
        ZL_E_SYNTAX, 1, "a list of two types");
    expect_refused(L,
        "let rec f x = f [x]\n",
        ZL_E_SYNTAX, 1, "an infinite type");
    expect_refused(L,
        "let x = 4611686018427387905\n",
        ZL_E_SYNTAX, 1, "a literal too large");
    expect_refused(L,
        "let () = if true then 1\n",
        ZL_E_SYNTAX, 1, "if without else of an int");
    expect_refused(L,
        "let x = 1\n"
        "print_int x\n",
        ZL_E_SYNTAX, 2, "an expression without ;; before it");
    expect_refused(L,
        "let () = print_int 1.0\n",
        ZL_E_UNSUPPORTED, 1, "float literal");

    /* the twin: canonical spelling, round trip and idempotence */
    expect_twin(L,
        "let   x=1;;let y = x+2 ;;\n",
        "let x = 1\n"
        "let y = x + 2\n");
    expect_twin(L,
        "print_string \"a\";;\n"
        "let _ = 1 + 2\n",
        "let () = print_string \"a\"\n"
        "let _ = 1 + 2\n");
    expect_twin(L,
        "(*  top  *)\n"
        "let x = 1 (* inside *) + 2\n"
        "\n"
        "(* end *)",
        "(* top *)\n"
        "let x = 1 + 2\n"
        "(* end *)\n");
    expect_twin(L,
        "let x = ((1 + 2)) * 3\n"
        "let y = 1 + (2 * 3)\n"
        "let z = (1 - 2) - 3\n"
        "let w = 1 - (2 - 3)\n",
        "let x = (1 + 2) * 3\n"
        "let y = 1 + 2 * 3\n"
        "let z = 1 - 2 - 3\n"
        "let w = 1 - (2 - 3)\n");
    expect_twin(L,
        "let a = - 5\n"
        "let b = -(5)\n"
        "let c = - a\n"
        "let d = abs (- a)\n",
        "let a = -5\n"
        "let b = -5\n"
        "let c = -a\n"
        "let d = abs (-a)\n");
    expect_twin(L,
        "let f x = match x with\n"
        "  | 0 -> (match 1 with 1 -> 2 | _ -> 3)\n"
        "  | n -> n\n",
        "let f x = match x with 0 -> (match 1 with 1 -> 2 | _ -> 3) | n -> n\n");
    expect_twin(L,
        "let f x = begin let a = x in a end\n",
        "let f x = let a = x in a\n");
    expect_twin(L,
        "let l = 1::2::[3;4]\n"
        "let m = (1::[])::[]\n",
        "let l = 1 :: 2 :: [3; 4]\n"
        "let m = (1 :: []) :: []\n");
    expect_twin(L,
        "let () = if true then (print_string \"a\"; print_string \"b\")\n",
        "let () = if true then (print_string \"a\"; print_string \"b\")\n");
    expect_twin(L,
        "let () = (if true then print_string \"a\"); print_newline ()\n",
        "let () = (if true then print_string \"a\"); print_newline ()\n");
    expect_twin(L,
        "let f = function | 0 -> 1 | n when n < 0 -> 2 | _ -> 3\n",
        "let f = function 0 -> 1 | n when n < 0 -> 2 | _ -> 3\n");
    expect_twin(L,
        "let b = (1 < 2 && 2 < 3) || (false && (true || false))\n",
        "let b = 1 < 2 && 2 < 3 || false && (true || false)\n");
    expect_twin(L,
        "let a = -(-5)\n"
        "let b = - (- a)\n"
        "let c = - -a\n",
        "let a = 5\n"
        "let b = -(-a)\n"
        "let c = -(-a)\n");
    expect_twin(L,
        "let a = (1 :: [2]) = [1; 2]\n"
        "let b = ([1] @ [2]) = (1 :: (2 :: []))\n",
        "let a = 1 :: [2] = [1; 2]\n"
        "let b = [1] @ [2] = 1 :: 2 :: []\n");
}
