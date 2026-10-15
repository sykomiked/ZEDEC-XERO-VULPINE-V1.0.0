/* test_zl_haskell.inc — Haskell (Haskell 2010, twin Purewyrm) front-end tests,
 * included by test_zl.c. Every output was checked against GHC 9.4.7
 * (runghc); every trap against GHC stopping with an error at the same
 * point of the output, or, for Int overflow and the strict arguments,
 * against GHC printing a value this subset will not guess; every SYNTAX
 * or UNDEFINED refusal against a GHC compile error. UNSUPPORTED refusals
 * are valid Haskell outside the subset. Each twin was run by GHC with the
 * same output as its source. */

static void test_haskell(void)
{
    const int L = ZL_LANG_HASKELL;

    /* outputs, each checked against the reference */
    expect_out(L,
        "main = putStrLn \"hello\"\n",
        "hello\n",
        "putStrLn");
    expect_out(L,
        "main = putStrLn \"a\\tb\\\\c\\\"d\"\n",
        "a\tb\\c\"d\n",
        "string escapes");
    expect_out(L,
        "main = do\n"
        "  putStr \"a\"\n"
        "  putStr \"b\\n\"\n",
        "ab\n",
        "putStr adds no newline");
    expect_out(L,
        "main = print 42\n",
        "42\n",
        "print an Int");
    expect_out(L,
        "main = print (-7)\n",
        "-7\n",
        "a negative number prints with -");
    expect_out(L,
        "main = print (1 + 2 * 3 - 4, (1 + 2) * 3, 10 - 3 - 2)\n",
        "(3,9,5)\n",
        "precedence and associativity");
    expect_out(L,
        "main = print (2 ^ 3 ^ 2, - 2 ^ 2, (-2) ^ 3, 2 ^ 0)\n",
        "(512,-4,-8,1)\n",
        "^ is right associative and binds tighter than unary minus");
    expect_out(L,
        "main = print (7 `div` 2, (-7) `div` 2, 7 `div` (-2), (-7) `div` (-2))\n",
        "(3,-4,-4,3)\n",
        "div floors");
    expect_out(L,
        "main = print (7 `mod` 2, (-7) `mod` 2, 7 `mod` (-2), (-7) `mod` (-2))\n",
        "(1,1,-1,-1)\n",
        "mod takes the sign of the divisor");
    expect_out(L,
        "main = print (quot (-7) 2, rem (-7) 2, quot 7 (-2), rem 7 (-2))\n",
        "(-3,-1,-3,1)\n",
        "quot and rem truncate");
    expect_out(L,
        "main = print (- 7 `div` 2, negate 7 `mod` 3)\n",
        "(-3,2)\n",
        "unary minus applies after div");
    expect_out(L,
        "main = print (abs (-3), signum (-4), signum 0, negate 5, succ 1, pred 1)\n",
        "(3,-1,0,-5,2,0)\n",
        "abs signum negate succ pred");
    expect_out(L,
        "main = print (min 3 (-4), max 3 (-4), gcd 12 (-18), lcm 4 6, gcd 0 0)\n",
        "(-4,3,6,12,0)\n",
        "min max gcd lcm");
    expect_out(L,
        "main = print (even 4, odd 4, even (-3), odd (-3))\n",
        "(True,False,False,True)\n",
        "even and odd");
    expect_out(L,
        "main = print (9223372036854775807, -9223372036854775807 - 1)\n",
        "(9223372036854775807,-9223372036854775808)\n",
        "the extreme Ints");
    expect_out(L,
        "main = print (True && False, True || False, not True, 1 < 2, 2 <= 1, 3 /= 3)\n",
        "(False,True,False,True,False,False)\n",
        "booleans and comparisons");
    expect_out(L,
        "main = print (False && 1 `div` 0 == 0, True || 1 `div` 0 == 0)\n",
        "(False,True)\n",
        "&& and || do not evaluate their right side when the left decides");
    expect_out(L,
        "main = print (True > False, max True False, min True False)\n",
        "(True,True,False)\n",
        "Bool is ordered");
    expect_out(L,
        "main = print ()\n",
        "()\n",
        "unit");
    expect_out(L,
        "main = print [1, -2, 3]\n",
        "[1,-2,3]\n",
        "a list prints with brackets and commas");
    expect_out(L,
        "main = print [[1], [], [2, 3]]\n",
        "[[1],[],[2,3]]\n",
        "a list of lists");
    expect_out(L,
        "main = print [True, False]\n",
        "[True,False]\n",
        "a list of Bools");
    expect_out(L,
        "main = print (1, [2], (True, -3))\n",
        "(1,[2],(True,-3))\n",
        "tuples in print");
    expect_out(L,
        "main = putStrLn (\"n = \" ++ show 3 ++ \", l = \" ++ show [1, 2] ++ show (-1) ++ show True)\n",
        "n = 3, l = [1,2]-1True\n",
        "show and ++");
    expect_out(L,
        "main = putStrLn (show (1, False))\n",
        "(1,False)\n",
        "show of a tuple");
    expect_out(L,
        "main = print $ 1 + 2\n",
        "3\n",
        "$");
    expect_out(L,
        "f :: Int -> Int\n"
        "f 0 = 1\n"
        "f n = n * f (n - 1)\n"
        "main = print (f 10)\n",
        "3628800\n",
        "equations with a literal pattern");
    expect_out(L,
        "f :: Int -> Int -> Int\n"
        "f a b\n"
        "  | a > b = a - b\n"
        "  | a < b = b - a\n"
        "  | otherwise = 0\n"
        "main = print (f 1 5, f 5 1, f 2 2)\n",
        "(4,4,0)\n",
        "guards and otherwise");
    expect_out(L,
        "f :: Int -> Int\n"
        "f x\n"
        "  | x > 0 = 1\n"
        "f _ = 0\n"
        "main = print (f 5, f (-5))\n",
        "(1,0)\n",
        "failing guards fall through to the next equation");
    expect_out(L,
        "f :: Int -> Int\n"
        "f x = y * 2\n"
        "  where y = x + 1\n"
        "main = print (f 3)\n",
        "8\n",
        "where");
    expect_out(L,
        "f :: Int -> Int\n"
        "f x\n"
        "  | x > lim = lim\n"
        "  | otherwise = x\n"
        "  where lim = 10\n"
        "main = print (f 3, f 30)\n",
        "(3,10)\n",
        "where scopes over the guards");
    expect_out(L,
        "main = print (let x = 2; y = x * 3 in x + y)\n",
        "8\n",
        "let ... in");
    expect_out(L,
        "main = print (let { a = 1; b = a + 1 } in a + b)\n",
        "3\n",
        "let with explicit braces");
    expect_out(L,
        "main = do\n"
        "  let x = 5\n"
        "      y = x + 1\n"
        "  print (x * y)\n",
        "30\n",
        "let in a do block");
    expect_out(L,
        "main = print (if 1 > 2 then 10 else 20)\n",
        "20\n",
        "if");
    expect_out(L,
        "main = do\n"
        "  let n = 4\n"
        "  if n > 3\n"
        "  then putStrLn \"big\"\n"
        "  else putStrLn \"small\"\n",
        "big\n",
        "if in a do block, then and else at the statement column");
    expect_out(L,
        "main = print (case 3 of\n"
        "  1 -> 10\n"
        "  3 -> 30\n"
        "  _ -> 0)\n",
        "30\n",
        "case with a layout block");
    expect_out(L,
        "main = print (case [1, 2] of { [] -> 0; (x:_) -> x })\n",
        "1\n",
        "case with braces and list patterns");
    expect_out(L,
        "f :: Int -> Int\n"
        "f n = case n of\n"
        "  k | k < 0 -> -1\n"
        "    | k == 0 -> 0\n"
        "  _ -> 1\n"
        "main = print (f (-5), f 0, f 5)\n",
        "(-1,0,1)\n",
        "guards in case alternatives");
    expect_out(L,
        "len :: [a] -> Int\n"
        "len [] = 0\n"
        "len (_:xs) = 1 + len xs\n"
        "main = print (len [1, 2, 3], len [True])\n",
        "(3,1)\n",
        "a polymorphic function");
    expect_out(L,
        "f :: [Int] -> Int\n"
        "f [a, b] = a + b\n"
        "f (a:b:_) = a * b\n"
        "f _ = 0\n"
        "main = print (f [3, 4], f [3, 4, 5], f [1])\n",
        "(7,12,0)\n",
        "fixed-length list patterns");
    expect_out(L,
        "isEven, isOdd :: Int -> Bool\n"
        "isEven 0 = True\n"
        "isEven n = isOdd (n - 1)\n"
        "isOdd 0 = False\n"
        "isOdd n = isEven (n - 1)\n"
        "main = print (isEven 10, isOdd 7)\n",
        "(True,True)\n",
        "mutual recursion");
    expect_out(L,
        "count :: Int -> Int -> Int\n"
        "count 0 s = s\n"
        "count n s = count (n - 1) (s + 1)\n"
        "main = print (count 100000 0)\n",
        "100000\n",
        "a tail call runs in constant space");
    expect_out(L,
        "loop :: Int -> IO ()\n"
        "loop i\n"
        "  | i > 3 = return ()\n"
        "  | otherwise = do\n"
        "      print i\n"
        "      loop (i + 1)\n"
        "main = loop 1\n",
        "1\n"
        "2\n"
        "3\n",
        "a recursive IO function");
    expect_out(L,
        "main :: IO ()\n"
        "main = do { print 1; print 2 }\n",
        "1\n"
        "2\n",
        "a do block with braces");
    expect_out(L,
        "module Main (main) where\n"
        "main = print 1\n",
        "1\n",
        "a module header");
    expect_out(L,
        "-- a comment\n"
        "{- a {- nested -} block -}\n"
        "main = print 1 -- trailing\n",
        "1\n",
        "comments");
    expect_out(L,
        "main = print xs\n"
        "  where xs = [1 .. 5]\n",
        "[1,2,3,4,5]\n",
        "a where on main");
    expect_out(L,
        "main = print ([1 .. 5], [5 .. 1], [1, 3 .. 10], [10, 7 .. 1], [2, 2 .. 1])\n",
        "([1,2,3,4,5],[],[1,3,5,7,9],[10,7,4,1],[])\n",
        "ranges");
    expect_out(L,
        "main = print (head [1, 2], tail [1, 2], last [1, 2], init [1, 2], null [], null [1], length [1, 2])\n",
        "(1,[2],2,[1],True,False,2)\n",
        "head tail last init null length");
    expect_out(L,
        "main = print (reverse [1, 2, 3], [1] ++ [2, 3], 0 : [1], [1, 2, 3] !! 1)\n",
        "([3,2,1],[1,2,3],[0,1],2)\n",
        "reverse ++ : !!");
    expect_out(L,
        "main = print (sum [1 .. 10], product [1 .. 5], maximum [3, 9, 2], minimum [3, 9, 2], sum [], product [])\n",
        "(55,120,9,2,0,1)\n",
        "sum product maximum minimum");
    expect_out(L,
        "main = print (take 2 [1, 2, 3], drop 2 [1, 2, 3], take (-1) [1], drop 5 [1], take 5 [1])\n",
        "([1,2],[3],[],[],[1])\n",
        "take and drop");
    expect_out(L,
        "main = print (elem 2 [1, 2], notElem 2 [1, 2], replicate 3 0, replicate (-1) 0)\n",
        "(True,False,[0,0,0],[])\n",
        "elem notElem replicate");
    expect_out(L,
        "main = print (concat [[1], [], [2, 3]], and [True, False], or [False, True], and [], or [])\n",
        "([1,2,3],False,True,True,False)\n",
        "concat and or");
    expect_out(L,
        "main = print ([1, 2] == [1, 2], [1] /= [1, 2], [1, 2] < [1, 3], [2] > [1, 9], [] < [0])\n",
        "(True,True,True,True,True)\n",
        "lists compare structurally");
    expect_out(L,
        "main = print (min [1] [2, 3], max [] [1])\n",
        "([1],[1])\n",
        "min and max of lists");
    expect_out(L,
        "f :: Int -> Int -> Int\n"
        "f a b = if b == 0 then 0 else q\n"
        "  where q = a `div` b\n"
        "main = print (f 7 0, f 7 2)\n",
        "(0,3)\n",
        "a where binding is evaluated only when used");
    expect_out(L,
        "main = do\n"
        "  let x = 1 `div` 0\n"
        "  print 5\n",
        "5\n",
        "an unused let is not evaluated");
    expect_out(L,
        "main = print (gcd 21 14 `max` 2 * 3)\n",
        "21\n",
        "backticks and precedence");
    expect_out(L,
        "main = print (3037000499 * 3037000499)\n",
        "9223372030926249001\n",
        "a large product");
    expect_out(L,
        "main = print (2 ^ 62)\n",
        "4611686018427387904\n",
        "2 ^ 62 in a program without lists");

    /* run-time traps: GHC stops with an error at the same point, or (overflow,
     * strict arguments) prints a value this subset does not reproduce */
    expect_trap(L,
        "main = print (9223372036854775807 + 1)\n",
        ZL_E_OVERFLOW, 1, "Int overflow stops the run (GHC would wrap)");
    expect_trap(L,
        "main = do\n"
        "  print 1\n"
        "  print (3037000500 * 3037000500)\n",
        ZL_E_OVERFLOW, 3, "multiplication overflows");
    expect_trap(L,
        "main = print (2 ^ 63)\n",
        ZL_E_OVERFLOW, 1, "^ overflows");
    expect_trap(L,
        "m :: Int\n"
        "m = -9223372036854775807 - 1\n"
        "main = print (negate m)\n",
        ZL_E_OVERFLOW, 3, "negating the smallest Int");
    expect_trap(L,
        "m :: Int\n"
        "m = -9223372036854775807 - 1\n"
        "main = print (abs m)\n",
        ZL_E_OVERFLOW, 3, "abs of the smallest Int");
    expect_trap(L,
        "m :: Int\n"
        "m = -9223372036854775807 - 1\n"
        "main = print (m `div` (-1))\n",
        ZL_E_OVERFLOW, 3, "div of the smallest Int by -1");
    expect_trap(L,
        "m :: Int\n"
        "m = -9223372036854775807 - 1\n"
        "main = print (m `quot` (-1))\n",
        ZL_E_OVERFLOW, 3, "quot of the smallest Int by -1");
    expect_trap(L,
        "main = print (succ 9223372036854775807)\n",
        ZL_E_OVERFLOW, 1, "succ of the largest Int");
    expect_trap(L,
        "main = print [4611686018427387903 + 1]\n",
        ZL_E_OVERFLOW, 1, "a program with lists has 62-bit numbers");
    expect_trap(L,
        "main = do\n"
        "  putStr \"a\"\n"
        "  print (1 `div` 0)\n",
        ZL_E_DIVZERO, 3, "division by zero");
    expect_trap(L,
        "main = print (5 `mod` 0)\n",
        ZL_E_DIVZERO, 1, "mod by zero");
    expect_trap(L,
        "main = print (rem 5 0)\n",
        ZL_E_DIVZERO, 1, "rem by zero");
    expect_trap(L,
        "main = print (2 ^ (-1))\n",
        ZL_E_DOMAIN, 1, "a negative exponent");
    expect_trap(L,
        "f :: [Int] -> Int\n"
        "f xs = head xs\n"
        "main = print (f [])\n",
        ZL_E_DOMAIN, 2, "head of an empty list");
    expect_trap(L,
        "f :: [Int] -> [Int]\n"
        "f xs = tail xs\n"
        "main = print (f [])\n",
        ZL_E_DOMAIN, 2, "tail of an empty list");
    expect_trap(L,
        "f :: [Int] -> Int\n"
        "f xs = maximum xs\n"
        "main = print (f [])\n",
        ZL_E_DOMAIN, 2, "maximum of an empty list");
    expect_trap(L,
        "main = print ([1, 2, 3] !! 5)\n",
        ZL_E_BOUNDS, 1, "!! past the end");
    expect_trap(L,
        "main = print ([1, 2, 3] !! (-1))\n",
        ZL_E_BOUNDS, 1, "!! with a negative index");
    expect_trap(L,
        "f :: Int -> Int\n"
        "f 0 = 1\n"
        "main = print (f 1)\n",
        ZL_E_DOMAIN, 2, "no equation matches");
    expect_trap(L,
        "f :: Int -> Int\n"
        "f n | n > 0 = 1\n"
        "main = print (f 0)\n",
        ZL_E_DOMAIN, 2, "no guard holds");
    expect_trap(L,
        "main = print (case 2 of { 1 -> 0 })\n",
        ZL_E_DOMAIN, 1, "no case alternative matches");
    expect_trap(L,
        "main = print [1, 1 .. 1]\n",
        ZL_E_DOMAIN, 1, "a range with step 0 is infinite");
    expect_trap(L,
        "f :: Int -> Int -> Int\n"
        "f a b = a\n"
        "main = print (f 1 (1 `div` 0))\n",
        ZL_E_DIVZERO, 3, "arguments are evaluated before the call (GHC would not)");

    /* refused when compiled, with the line of the cause */
    expect_refused(L,
        "data T = A | B\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "data declarations");
    expect_refused(L,
        "type N = Int\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "type synonyms");
    expect_refused(L,
        "class C a where\n"
        "  c :: a -> Int\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "type classes");
    expect_refused(L,
        "instance Num Bool\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "instances");
    expect_refused(L,
        "import Data.List\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "imports");
    expect_refused(L,
        "module Foo where\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "a module other than Main");
    expect_refused(L,
        "main = print (map negate [1, 2])\n",
        ZL_E_UNSUPPORTED, 1, "map");
    expect_refused(L,
        "main = print (filter even [1, 2])\n",
        ZL_E_UNSUPPORTED, 1, "filter");
    expect_refused(L,
        "main = print (foldr max 0 [1])\n",
        ZL_E_UNSUPPORTED, 1, "foldr");
    expect_refused(L,
        "main = print ((\\x -> x) 1)\n",
        ZL_E_UNSUPPORTED, 1, "lambdas");
    expect_refused(L,
        "main = print ((+ 1) 2)\n",
        ZL_E_UNSUPPORTED, 1, "sections");
    expect_refused(L,
        "f :: Int -> Int\n"
        "f = negate\n"
        "main = print (f 1)\n",
        ZL_E_UNSUPPORTED, 2, "a function as a value");
    expect_refused(L,
        "f :: Int -> Int -> Int\n"
        "f a b = a\n"
        "main = print (f 1 2)\n"
        "  where g = f 1\n",
        ZL_E_UNSUPPORTED, 4, "partial application");
    expect_refused(L,
        "main = print 1.5\n",
        ZL_E_UNSUPPORTED, 1, "floating point");
    expect_refused(L,
        "main = print (sqrt 2)\n",
        ZL_E_UNSUPPORTED, 1, "sqrt");
    expect_refused(L,
        "f :: Integer -> Integer\n"
        "f x = x\n"
        "main = print (f 1)\n",
        ZL_E_UNSUPPORTED, 1, "Integer");
    expect_refused(L,
        "main = print 9223372036854775808\n",
        ZL_E_UNSUPPORTED, 1, "a literal past Int");
    expect_refused(L,
        "main = print (1 :: Int)\n",
        ZL_E_UNSUPPORTED, 1, "type annotations in expressions");
    expect_refused(L,
        "main = putStrLn ['a']\n",
        ZL_E_UNSUPPORTED, 1, "characters");
    expect_refused(L,
        "main = print (length \"abc\")\n",
        ZL_E_UNSUPPORTED, 1, "strings as values");
    expect_refused(L,
        "f :: Int -> String\n"
        "f x = show x\n"
        "main = putStrLn (f 1)\n",
        ZL_E_UNSUPPORTED, 1, "String in a signature");
    expect_refused(L,
        "main = print (show 1)\n",
        ZL_E_UNSUPPORTED, 1, "show as a value");
    expect_refused(L,
        "main = do\n"
        "  x <- return 1\n"
        "  print x\n",
        ZL_E_UNSUPPORTED, 2, "<- in a do block");
    expect_refused(L,
        "main = mapM_ print [1, 2]\n",
        ZL_E_UNSUPPORTED, 1, "mapM_");
    expect_refused(L,
        "main = getLine >>= putStrLn\n",
        ZL_E_UNSUPPORTED, 1, "input and >>=");
    expect_refused(L,
        "main = print [1 ..]\n",
        ZL_E_UNSUPPORTED, 1, "infinite ranges");
    expect_refused(L,
        "main = print [x | x <- [1, 2]]\n",
        ZL_E_UNSUPPORTED, 1, "list comprehensions");
    expect_refused(L,
        "main = print (let xs = 1 : xs in head xs)\n",
        ZL_E_UNSUPPORTED, 1, "a recursive value");
    expect_refused(L,
        "f x = (x, x)\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "a tuple as a value");
    expect_refused(L,
        "main = print (fst (1, 2))\n",
        ZL_E_UNSUPPORTED, 1, "fst");
    expect_refused(L,
        "main = print (Just 1)\n",
        ZL_E_UNSUPPORTED, 1, "constructors");
    expect_refused(L,
        "main = print (seq 1 2)\n",
        ZL_E_UNSUPPORTED, 1, "seq");
    expect_refused(L,
        "main = print (compare 1 2)\n",
        ZL_E_UNSUPPORTED, 1, "compare and Ordering");
    expect_refused(L,
        "main = print (error \"x\")\n",
        ZL_E_UNSUPPORTED, 1, "error");
    expect_refused(L,
        "x = 1\n"
        "main = do\n"
        "  let a = print 1\n"
        "  a\n",
        ZL_E_UNSUPPORTED, 3, "an IO action as a value");
    expect_refused(L,
        "m :: Int\n"
        "m = maxBound\n"
        "main = print m\n",
        ZL_E_UNSUPPORTED, 2, "maxBound");
    expect_refused(L,
        "a +++ b = a\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "operator definitions");
    expect_refused(L,
        "main = do\n"
        "\tprint 1\n",
        ZL_E_UNSUPPORTED, 2, "tabs");
    expect_refused(L,
        "{-# LANGUAGE BangPatterns #-}\n"
        "main = print 1\n",
        ZL_E_UNSUPPORTED, 1, "pragmas");
    expect_refused(L,
        "main = print (1 + * 2)\n",
        ZL_E_SYNTAX, 1, "a missing operand");
    expect_refused(L,
        "main = print (1 == 2 == 3)\n",
        ZL_E_SYNTAX, 1, "non-associative operators");
    expect_refused(L,
        "main = print (1 + True)\n",
        ZL_E_SYNTAX, 1, "a type error");
    expect_refused(L,
        "main = print []\n",
        ZL_E_SYNTAX, 1, "an ambiguous type");
    expect_refused(L,
        "f :: Int -> Bool\n"
        "f x = x\n"
        "main = print (f 1)\n",
        ZL_E_SYNTAX, 2, "a signature that does not match");
    expect_refused(L,
        "f :: a -> Int\n"
        "f x = x\n"
        "main = print (f 1)\n",
        ZL_E_SYNTAX, 1, "a signature more general than the equations");
    expect_refused(L,
        "main = print y\n",
        ZL_E_UNDEFINED, 1, "an unknown name");
    expect_refused(L,
        "f = 1\n",
        ZL_E_UNDEFINED, 0, "no main");
    expect_refused(L,
        "f 0 = 1\n"
        "g = 2\n"
        "f n = 3\n"
        "main = print (f 1)\n",
        ZL_E_SYNTAX, 3, "equations of one function apart");
    expect_refused(L,
        "f x x = 1\n"
        "main = print (f 1 2)\n",
        ZL_E_SYNTAX, 1, "a variable bound twice");
    expect_refused(L,
        "main = print (1 + - 2)\n",
        ZL_E_SYNTAX, 1, "a minus sign after an operator");
    expect_refused(L,
        "main = print 1\n"
        "main = print 2\n",
        ZL_E_SYNTAX, 2, "main defined twice");
    expect_refused(L,
        "main = print 1\n"
        "f :: Int\n",
        ZL_E_SYNTAX, 2, "a signature without a binding");
    expect_refused(L,
        "main = putStrLn 1\n",
        ZL_E_SYNTAX, 1, "putStrLn of a number");
    expect_refused(L,
        "main = print (print 1)\n",
        ZL_E_SYNTAX, 1, "show of an IO action");
    expect_refused(L,
        "main = 1\n",
        ZL_E_SYNTAX, 1, "main is not IO");

    /* twins: the canonical form, which GHC runs with the same output */
    expect_twin(L,
        "main = print 1\n",
        "main = print 1\n");
    expect_twin(L,
        "module Main where\n"
        "\n"
        "-- c\n"
        "f :: Int -> Int\n"
        "f x\n"
        "  | x > 0 = x\n"
        "  | otherwise = negate x\n"
        "  where\n"
        "    y = 1\n"
        "\n"
        "main = do\n"
        "  print (f (-3))\n"
        "  let z = [1..3]\n"
        "  print $ z ++ [4]\n",
        "-- c\n"
        "f :: Int -> Int\n"
        "f x | x > 0 = x | otherwise = negate x where { y = 1 }\n"
        "main = do { print (f (-3)); let { z = [1 .. 3] }; print (z ++ [4]) }\n");
    expect_twin(L,
        "g :: [Int] -> Int\n"
        "g (a:b:_) = a `div` b\n"
        "g _ = 0\n"
        "main = print (case [1,2] of\n"
        "  [] -> 0\n"
        "  (x:_) -> if x > 0 then g [4,2] else 1)\n",
        "g :: [Int] -> Int\n"
        "g (a : b : _) = a `div` b\n"
        "g _ = 0\n"
        "main = print (case [1, 2] of { [] -> 0; x : _ -> if x > 0 then g [4, 2] else 1 })\n");
    expect_twin(L,
        "main = putStrLn (\"x\" ++ show (-1) ++ show (2 - (-3)) ++ show (negate 2 ^ 2))\n",
        "main = putStrLn (\"x\" ++ show (-1) ++ show (2 - (-3)) ++ show (negate 2 ^ 2))\n");
    expect_twin(L,
        "main = do\n"
        "  let a = 1\n"
        "  print (let b = a + 1 in b * (a - b))\n",
        "main = do { let { a = 1 }; print (let { b = a + 1 } in b * (a - b)) }\n");
}
