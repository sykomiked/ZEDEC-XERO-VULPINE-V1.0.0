/* test_zl_ml.inc — ML (Standard ML '97, twin Proofdrake) front-end tests,
 * included by test_zl.c. Generated from a case list in which every output
 * was checked against MLton 20210117, every trap against an uncaught MLton
 * exception at the same point of the output, and every SYNTAX or UNDEFINED
 * refusal against an MLton compile error. UNSUPPORTED refusals are valid ML
 * outside the subset. */

static void test_ml(void)
{
    const int L = ZL_LANG_ML;

    /* outputs, each checked against the reference */
    expect_out(L,
        "val _ = print \"hello\\n\"\n",
        "hello\n",
        "print a literal");
    expect_out(L,
        "val _ = print \"a\\tb\\\\c\\\"d\\n\"\n",
        "a\tb\\c\"d\n",
        "string escapes");
    expect_out(L,
        "val _ = print (Int.toString (42) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~7) ^ \"\\n\")\n",
        "42\n"
        "~7\n",
        "Int.toString writes ~");
    expect_out(L,
        "val _ = print (Int.toString (~2147483648) ^ \"\\n\")\n"
        "val _ = print (Int.toString (2147483647) ^ \"\\n\")\n",
        "~2147483648\n"
        "2147483647\n",
        "the extreme ints");
    expect_out(L,
        "val _ = print (Int.toString (1 + 2 * 3 - 4) ^ \"\\n\")\n"
        "val _ = print (Int.toString ((1 + 2) * 3) ^ \"\\n\")\n"
        "val _ = print (Int.toString (10 - 3 - 2) ^ \"\\n\")\n",
        "3\n"
        "9\n"
        "5\n",
        "precedence and associativity");
    expect_out(L,
        "val _ = print (Int.toString (7 div 2) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~7 div 2) ^ \"\\n\")\n"
        "val _ = print (Int.toString (7 div ~2) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~7 div ~2) ^ \"\\n\")\n",
        "3\n"
        "~4\n"
        "~4\n"
        "3\n",
        "div floors");
    expect_out(L,
        "val _ = print (Int.toString (7 mod 2) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~7 mod 2) ^ \"\\n\")\n"
        "val _ = print (Int.toString (7 mod ~2) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~7 mod ~2) ^ \"\\n\")\n",
        "1\n"
        "1\n"
        "~1\n"
        "~1\n",
        "mod takes the sign of the divisor");
    expect_out(L,
        "val _ = print (Int.toString (Int.quot (~7, 2)) ^ \"\\n\")\n"
        "val _ = print (Int.toString (Int.rem (~7, 2)) ^ \"\\n\")\n"
        "val _ = print (Int.toString (Int.rem (7, ~2)) ^ \"\\n\")\n",
        "~3\n"
        "~1\n"
        "1\n",
        "quot and rem truncate");
    expect_out(L,
        "val _ = print (Int.toString (abs ~3) ^ \"\\n\")\n"
        "val _ = print (Int.toString (Int.abs 4) ^ \"\\n\")\n"
        "val _ = print (Int.toString (Int.min (2, ~5)) ^ \"\\n\")\n"
        "val _ = print (Int.toString (Int.max (2, ~5)) ^ \"\\n\")\n",
        "3\n"
        "4\n"
        "~5\n"
        "2\n",
        "abs min max");
    expect_out(L,
        "val x = 5\n"
        "val _ = print (Int.toString (~x) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~ (2 + 3)) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~ 5) ^ \"\\n\")\n"
        "val _ = print (Int.toString (~(~x)) ^ \"\\n\")\n",
        "~5\n"
        "~5\n"
        "~5\n"
        "5\n",
        "negation");
    expect_out(L,
        "val _ = print (Bool.toString (1 < 2) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (2 <= 1) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (3 > 2) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (3 >= 4) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (1 = 1) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (1 <> 1) ^ \"\\n\")\n",
        "true\n"
        "false\n"
        "true\n"
        "false\n"
        "true\n"
        "false\n",
        "comparisons");
    expect_out(L,
        "val _ = print (Bool.toString (false andalso 1 div 0 = 1) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (true orelse 1 div 0 = 1) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (not (1 = 2)) ^ \"\\n\")\n",
        "false\n"
        "true\n"
        "true\n",
        "andalso and orelse short-circuit");
    expect_out(L,
        "val _ = print (Bool.toString (true = false) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (() = ()) ^ \"\\n\")\n",
        "false\n"
        "true\n",
        "equality of booleans and unit");
    expect_out(L,
        "val _ = print (Int.toString (if 1 < 2 then 10 else 20) ^ \"\\n\")\n"
        "val _ = print (Int.toString (if 1 > 2 then 10 else if 2 > 1 then 30 else 40) ^ \"\\n\")\n",
        "10\n"
        "30\n",
        "if");
    expect_out(L,
        "fun f n = case n of 0 => 100 | ~1 => 200 | _ => n\n"
        "val _ = print (Int.toString (f 0) ^ \"\\n\")\n"
        "val _ = print (Int.toString (f ~1) ^ \"\\n\")\n"
        "val _ = print (Int.toString (f 9) ^ \"\\n\")\n",
        "100\n"
        "200\n"
        "9\n",
        "case on integers");
    expect_out(L,
        "fun f b = case b of true => 1 | false => 0\n"
        "val _ = print (Int.toString (f true + f false) ^ \"\\n\")\n",
        "1\n",
        "case on booleans");
    expect_out(L,
        "val x = let val a = 2 val b = a * 3 in a + b end\n"
        "val _ = print (Int.toString (x) ^ \"\\n\")\n",
        "8\n",
        "let");
    expect_out(L,
        "val _ = let val a = 1 in print \"a\"; print (Int.toString a); print \"\\n\" end\n",
        "a1\n",
        "let with a sequence");
    expect_out(L,
        "fun fact 0 = 1\n"
        "  | fact n = n * fact (n - 1)\n"
        "val _ = print (Int.toString (fact 10) ^ \"\\n\")\n",
        "3628800\n",
        "clauses");
    expect_out(L,
        "fun add a b c = a + b * c\n"
        "val _ = print (Int.toString (add 1 2 3) ^ \"\\n\")\n",
        "7\n",
        "curried parameters");
    expect_out(L,
        "fun gcd (a, 0) = a\n"
        "  | gcd (a, b) = gcd (b, a mod b)\n"
        "val _ = print (Int.toString (gcd (1071, 462)) ^ \"\\n\")\n",
        "21\n",
        "tupled parameters");
    expect_out(L,
        "fun f (a, b) c = a * 100 + b * 10 + c\n"
        "val _ = print (Int.toString (f (1, 2) 3) ^ \"\\n\")\n",
        "123\n",
        "tupled and curried");
    expect_out(L,
        "fun f n = let fun g x = x + n fun h y = g y * 2 in h 1 end\n"
        "val _ = print (Int.toString (f 10) ^ \"\\n\")\n",
        "22\n",
        "nested functions read their parents");
    expect_out(L,
        "fun even 0 = true\n"
        "  | even n = odd (n - 1)\n"
        "and odd 0 = false\n"
        "  | odd n = even (n - 1)\n"
        "val _ = print (Bool.toString (even 10) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (odd 10) ^ \"\\n\")\n",
        "true\n"
        "false\n",
        "mutual recursion");
    expect_out(L,
        "fun loop (0, s) = s\n"
        "  | loop (n, s) = loop (n - 1, s + 1)\n"
        "val _ = print (Int.toString (loop (100000, 0)) ^ \"\\n\")\n",
        "100000\n",
        "a tail call is a loop");
    expect_out(L,
        "val (a, b) = (3, 4)\n"
        "val _ = print (Int.toString (a * b) ^ \"\\n\")\n",
        "12\n",
        "a tuple binding");
    expect_out(L,
        "val x = 1\n"
        "val x = x + 1\n"
        "val _ = print (Int.toString (x) ^ \"\\n\")\n",
        "2\n",
        "shadowing");
    expect_out(L,
        "fun f x = (print (Int.toString x); x)\n"
        "val _ = print (Int.toString (f 1 + f 2) ^ \"\\n\")\n"
        "val _ = print (Int.toString (f 3) ^ Int.toString (f 4) ^ \"\\n\")\n",
        "123\n"
        "3434\n",
        "left-to-right evaluation");
    expect_out(L,
        "val _ = print \"a\";\n"
        "print \"b\\n\";\n"
        "val _ = print \"c\\n\";\n",
        "ab\n"
        "c\n",
        "top-level expressions");
    expect_out(L,
        "(* a (* nested *) comment *)\n"
        "val _ = print \"x\\n\" (* after *)\n",
        "x\n",
        "comments");
    expect_out(L,
        "val l = [1, 2, 3]\n"
        "val _ = print (Int.toString (hd l) ^ \"\\n\")\n"
        "val _ = print (Int.toString (hd (tl l)) ^ \"\\n\")\n"
        "val _ = print (Int.toString (length l) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (null l) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (null []) ^ \"\\n\")\n",
        "1\n"
        "2\n"
        "3\n"
        "false\n"
        "true\n",
        "list basics");
    expect_out(L,
        "fun sum [] = 0\n"
        "  | sum (x :: xs) = x + sum xs\n"
        "val _ = print (Int.toString (sum [1, 2, 3, 4]) ^ \"\\n\")\n"
        "val _ = print (Int.toString (sum (rev [5, 6] @ [7])) ^ \"\\n\")\n"
        "val _ = print (Int.toString (List.length [true]) ^ \"\\n\")\n",
        "10\n"
        "18\n"
        "1\n",
        "list recursion");
    expect_out(L,
        "fun f [] = 0\n"
        "  | f [x] = x\n"
        "  | f [x, y] = x * y\n"
        "  | f (x :: y :: _) = x + y\n"
        "val _ = print (Int.toString (f []) ^ \"\\n\")\n"
        "val _ = print (Int.toString (f [7]) ^ \"\\n\")\n"
        "val _ = print (Int.toString (f [3, 4]) ^ \"\\n\")\n"
        "val _ = print (Int.toString (f [3, 4, 5]) ^ \"\\n\")\n",
        "0\n"
        "7\n"
        "12\n"
        "7\n",
        "list patterns");
    expect_out(L,
        "val _ = print (Bool.toString ([1, 2] = [1, 2]) ^ \"\\n\")\n"
        "val _ = print (Bool.toString ([1, 2] = [1]) ^ \"\\n\")\n"
        "val _ = print (Bool.toString ([[1], []] <> [[1], []]) ^ \"\\n\")\n"
        "val _ = print (Bool.toString ([true] = [true]) ^ \"\\n\")\n",
        "true\n"
        "false\n"
        "false\n"
        "true\n",
        "structural equality");
    expect_out(L,
        "fun len [] = 0\n"
        "  | len (_ :: t) = 1 + len t\n"
        "val _ = print (Int.toString (len [1, 2] + len [true]) ^ \"\\n\")\n",
        "3\n",
        "a polymorphic function");
    expect_out(L,
        "val e = []\n"
        "val a = 1 :: e\n"
        "val b = true :: e\n"
        "val _ = print (Int.toString (length a + length b) ^ \"\\n\")\n",
        "2\n",
        "a polymorphic value");
    expect_out(L,
        "fun eq (a, b) = a = b\n"
        "val _ = print (Bool.toString (eq ([1], [1])) ^ \"\\n\")\n"
        "val _ = print (Bool.toString (eq (1, 2)) ^ \"\\n\")\n",
        "true\n"
        "false\n",
        "polymorphic equality");
    expect_out(L,
        "val _ = print (Bool.toString (1 < 2) ^ \"/\" ^ Bool.toString false ^ \"\\n\")\n",
        "true/false\n",
        "Bool.toString");
    expect_out(L,
        "val a = 1; val b = 2;\n"
        "val _ = print (Int.toString (a + b) ^ \"\\n\")\n",
        "3\n",
        "semicolons between declarations");
    expect_out(L,
        "val _ = (print \"x\"; print \"y\\n\")\n",
        "xy\n",
        "a sequence");
    expect_out(L,
        "val _ = print \"\"\n"
        "val () = print \"u\\n\"\n",
        "u\n",
        "unit patterns");
    expect_out(L,
        "fun f () = 5\n"
        "val _ = print (Int.toString (f ()) ^ \"\\n\")\n",
        "5\n",
        "a unit parameter");
    expect_out(L,
        "val _ = print (Bool.toString (1 :: [2] = [1, 2]) ^ Bool.toString ([1] @ [2] = 1 :: [3]) ^ \"\\n\")\n",
        "truefalse\n",
        "an operator after :: and @");

    /* run-time traps: the reference stops with an uncaught exception */
    expect_trap(L,
        "val _ = print (Int.toString (2147483647 + 1) ^ \"\\n\")\n",
        ZL_E_OVERFLOW, 1, "addition overflows");
    expect_trap(L,
        "val _ = print (Int.toString (65536 * 32768) ^ \"\\n\")\n",
        ZL_E_OVERFLOW, 1, "multiplication overflows");
    expect_trap(L,
        "val m = ~2147483648\n"
        "val _ = print (Int.toString (~m) ^ \"\\n\")\n",
        ZL_E_OVERFLOW, 2, "negating the smallest int");
    expect_trap(L,
        "val m = ~2147483648\n"
        "val _ = print (Int.toString (m div ~1) ^ \"\\n\")\n",
        ZL_E_OVERFLOW, 2, "dividing the smallest int by ~1");
    expect_trap(L,
        "val m = ~2147483648\n"
        "val _ = print (Int.toString (abs m) ^ \"\\n\")\n",
        ZL_E_OVERFLOW, 2, "abs of the smallest int");
    expect_trap(L,
        "val l = [2147483647]\n"
        "val _ = print (Int.toString (hd l + 1) ^ \"\\n\")\n",
        ZL_E_OVERFLOW, 2, "overflow in a list program");
    expect_trap(L,
        "val _ = print \"a\"\n"
        "val _ = print (Int.toString (1 div 0) ^ \"\\n\")\n",
        ZL_E_DIVZERO, 2, "division by zero");
    expect_trap(L,
        "val _ = print (Int.toString (1 mod 0) ^ \"\\n\")\n",
        ZL_E_DIVZERO, 1, "mod by zero");
    expect_trap(L,
        "fun f 0 = 1\n"
        "val _ = print (Int.toString (f 2) ^ \"\\n\")\n",
        ZL_E_DOMAIN, 1, "a match that fails");
    expect_trap(L,
        "val _ = print (Int.toString (hd []) ^ \"\\n\")\n",
        ZL_E_DOMAIN, 1, "hd of the empty list");
    expect_trap(L,
        "val [a] = [1, 2]\n",
        ZL_E_DOMAIN, 1, "a binding that fails");

    /* refusals, with the line they are reported on */
    expect_refused(L,
        "val x = 1.5\n",
        ZL_E_UNSUPPORTED, 1, "reals");
    expect_refused(L,
        "val x = 0x1F\n",
        ZL_E_UNSUPPORTED, 1, "hexadecimal");
    expect_refused(L,
        "val c = #\"a\"\n",
        ZL_E_UNSUPPORTED, 1, "characters");
    expect_refused(L,
        "val s = \"abc\"\n",
        ZL_E_UNSUPPORTED, 1, "a string value");
    expect_refused(L,
        "val f = fn x => x\n",
        ZL_E_UNSUPPORTED, 1, "fn");
    expect_refused(L,
        "fun f x = x\n"
        "val g = f\n",
        ZL_E_UNSUPPORTED, 2, "a function as a value");
    expect_refused(L,
        "fun f a b = a\n"
        "val g = f 1\n",
        ZL_E_UNSUPPORTED, 2, "partial application");
    expect_refused(L,
        "datatype t = A | B\n",
        ZL_E_UNSUPPORTED, 1, "datatype");
    expect_refused(L,
        "exception E\n",
        ZL_E_UNSUPPORTED, 1, "exceptions");
    expect_refused(L,
        "val r = ref 0\n",
        ZL_E_UNSUPPORTED, 1, "references");
    expect_refused(L,
        "val x = (1, 2)\n",
        ZL_E_UNSUPPORTED, 1, "a tuple value");
    expect_refused(L,
        "val x = {a = 1}\n",
        ZL_E_UNSUPPORTED, 1, "records");
    expect_refused(L,
        "structure S = struct end\n",
        ZL_E_UNSUPPORTED, 1, "structures");
    expect_refused(L,
        "val x : int = 1\n",
        ZL_E_UNSUPPORTED, 1, "type annotations");
    expect_refused(L,
        "val x = 1 / 2\n",
        ZL_E_UNSUPPORTED, 1, "real division");
    expect_refused(L,
        "val x = map\n",
        ZL_E_UNSUPPORTED, 1, "map");
    expect_refused(L,
        "fun f x = while x do ()\n",
        ZL_E_UNSUPPORTED, 1, "while");
    expect_refused(L,
        "val x = y\n",
        ZL_E_UNDEFINED, 1, "an unbound name");
    expect_refused(L,
        "val x = 1 + true\n",
        ZL_E_SYNTAX, 1, "a type error");
    expect_refused(L,
        "val x = if 1 then 2 else 3\n",
        ZL_E_SYNTAX, 1, "a condition that is not boolean");
    expect_refused(L,
        "val x = [1, true]\n",
        ZL_E_SYNTAX, 1, "a list of two types");
    expect_refused(L,
        "fun f x = f [x]\n",
        ZL_E_SYNTAX, 1, "an infinite type");
    expect_refused(L,
        "val x = 1\n"
        "val y = x 2\n",
        ZL_E_SYNTAX, 2, "applying a number");
    expect_refused(L,
        "val x = 2147483648\n",
        ZL_E_SYNTAX, 1, "a literal too large");
    expect_refused(L,
        "val x = case 1 of | 1 => 2\n",
        ZL_E_SYNTAX, 1, "a bar before the first rule");
    expect_refused(L,
        "val x = 1 + if true then 1 else 2\n",
        ZL_E_SYNTAX, 1, "if as an operand");
    expect_refused(L,
        "val x = 1\n"
        "print \"a\";\n",
        ZL_E_SYNTAX, 2, "an expression without ; before it");
    expect_refused(L,
        "fun f 0 = 1\n"
        "  | g n = 2\n",
        ZL_E_SYNTAX, 2, "clauses that name another function");
    expect_refused(L,
        "val x = 1 < [1]\n",
        ZL_E_SYNTAX, 1, "ordering a list");
    expect_refused(L,
        "val _ = print 5\n",
        ZL_E_SYNTAX, 1, "print of a number");

    /* the twin: canonical spelling, round trip and idempotence */
    expect_twin(L,
        "val   x=1;val y = x+2 ;\n",
        "val x = 1\n"
        "val y = x + 2\n");
    expect_twin(L,
        "print \"a\";\n"
        "val _=print\"b\"\n",
        "val _ = print \"a\"\n"
        "val _ = print \"b\"\n");
    expect_twin(L,
        "(*  top  *)\n"
        "val x = 1 (* inside *) + 2\n"
        "\n"
        "(* end *)",
        "(* top *)\n"
        "val x = 1 + 2\n"
        "(* end *)\n");
    expect_twin(L,
        "val x = ((1 + 2)) * 3\n"
        "val y = 1 + (2 * 3)\n"
        "val z = (1 - 2) - 3\n"
        "val w = 1 - (2 - 3)\n",
        "val x = (1 + 2) * 3\n"
        "val y = 1 + 2 * 3\n"
        "val z = 1 - 2 - 3\n"
        "val w = 1 - (2 - 3)\n");
    expect_twin(L,
        "val a = ~ 5\n"
        "val b = ~(5)\n"
        "val c = ~ a\n",
        "val a = ~5\n"
        "val b = ~5\n"
        "val c = ~a\n");
    expect_twin(L,
        "fun f 0 = (case 1 of 1 => 2 | _ => 3) | f n = case n of 1 => 4 | _ => 5\n",
        "fun f 0 = (case 1 of 1 => 2 | _ => 3)\n"
        "  | f n = case n of 1 => 4 | _ => 5\n");
    expect_twin(L,
        "fun f x = let val a = x\n"
        "val b = a in (a ; b) end\n",
        "fun f x = let val a = x val b = a in a; b end\n");
    expect_twin(L,
        "val l = 1::2::[3,4]\n"
        "val m = (1::[])::[]\n",
        "val l = 1 :: 2 :: [3, 4]\n"
        "val m = (1 :: []) :: []\n");
    expect_twin(L,
        "fun f(x::y)=x|f[]=0\n",
        "fun f (x :: y) = x\n"
        "  | f [] = 0\n");
    expect_twin(L,
        "val b = (1 < 2 andalso 2 < 3) orelse (false andalso (true orelse false))\n",
        "val b = 1 < 2 andalso 2 < 3 orelse false andalso (true orelse false)\n");
    expect_twin(L,
        "val x = if true then if false then 1 else 2 else 3\n",
        "val x = if true then (if false then 1 else 2) else 3\n");
    expect_twin(L,
        "val a = ~(~5)\n"
        "val b = ~ (~ a)\n",
        "val a = 5\n"
        "val b = ~(~a)\n");
    expect_twin(L,
        "val a = (1 :: [2]) = [1, 2]\n"
        "val b = ([1] @ [2]) = (1 :: (2 :: []))\n",
        "val a = 1 :: [2] = [1, 2]\n"
        "val b = [1] @ [2] = 1 :: 2 :: []\n");
}
