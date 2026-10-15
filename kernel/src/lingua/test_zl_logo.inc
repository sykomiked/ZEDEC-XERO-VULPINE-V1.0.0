/* test_zl_logo.inc — Logo front-end tests, included by test_zl.c. The
 * expect_out cases were checked against UCBLogo 6.2 when written. */

static void test_logo(void)
{
    const int G = ZL_LANG_LOGO;
    expect_out(G, "print 42\n", "42\n", "print a number");
    expect_out(G, "print -7\n", "-7\n", "print a negative number");
    expect_out(G, "pr 1\nprint 2\n", "1\n2\n", "pr abbreviates print");
    expect_out(G, "print \"hello\n", "hello\n", "print a word");
    expect_out(G, "show \"hello\n", "hello\n", "show a word");
    expect_out(G, "print [a b [c d] 3]\n", "a b [c d] 3\n", "print a list drops its brackets");
    expect_out(G, "show [a b [c d] 3]\n", "[a b [c d] 3]\n", "show keeps the brackets");
    expect_out(G, "print []\n", "\n", "print the empty list");
    expect_out(G, "show []\n", "[]\n", "show the empty list");
    expect_out(G, "show [[] [[]]]\n", "[[] [[]]]\n", "nested empty lists");
    expect_out(G, "type 1\ntype \"a\ntype [b c]\nprint []\n", "1ab c\n", "type has no newline");
    expect_out(G, "(print 1 [2 3] \"a [])\n", "1 2 3 a \n", "print several inputs");
    expect_out(G, "(show 1 [2 3] \"a)\n", "1 [2 3] a\n", "show several inputs");
    expect_out(G, "(type 1 2 [3 4] \"a)\nprint []\n", "123 4a\n", "type several inputs");
    expect_out(G, "(print 7)\n", "7\n", "print in parentheses with one input");
    expect_out(G, "(print)\n", "\n", "print with no inputs");
    expect_out(G, "print \"true\nprint \"false\n", "true\nfalse\n", "boolean words");
    expect_out(G, "print 1 = 1\nprint 1 = 2\n", "true\nfalse\n", "comparisons give words");
    expect_out(G, "print 999999999999999\nprint -999999999999999\n",
               "999999999999999\n-999999999999999\n", "largest literals");
    expect_out(G, "print \"Hello_World\n", "Hello_World\n", "a word keeps its spelling");
    expect_out(G, "print [ 1   2 ]\n", "1 2\n", "spaces inside a list");
    expect_out(G, "print 1 + 2 * 3\n", "7\n", "precedence");
    expect_out(G, "print (1 + 2) * 3\n", "9\n", "parentheses");
    expect_out(G, "print 10 - 3 - 2\n", "5\n", "subtraction is left associative");
    expect_out(G, "print 100 / 5 / 2\n", "10\n", "division is left associative");
    expect_out(G, "print 2 * 3 + 4 * 5\n", "26\n", "products before sums");
    expect_out(G, "print 1+2*3-4\n", "3\n", "operators without spaces");
    expect_out(G, "print sum 1 2\nprint difference 10 3\nprint product 6 7\nprint quotient 20 4\n",
               "3\n7\n42\n5\n", "arithmetic procedures");
    expect_out(G, "print sum 1 2 * 3\n", "7\n", "a prefix input is a whole expression");
    expect_out(G, "print (sum 1 2 3 4)\nprint (product 2 3 4)\n", "10\n24\n",
               "variadic sum and product");
    expect_out(G, "print (sum 5)\nprint (sum)\nprint (product)\n", "5\n0\n1\n",
               "variadic with few inputs");
    expect_out(
        G,
        "print remainder 7 3\nprint remainder -7 3\nprint remainder 7 -3\nprint remainder -7 -3\n",
        "1\n-1\n1\n-1\n", "remainder takes the sign of the dividend");
    expect_out(G, "print modulo 7 3\nprint modulo -7 3\nprint modulo 7 -3\nprint modulo -7 -3\n",
               "1\n2\n-2\n-1\n", "modulo takes the sign of the divisor");
    expect_out(G, "print modulo 6 -3\nprint modulo 0 -3\nprint modulo 6 3\nprint modulo 0 3\n",
               "-3\n-3\n0\n0\n", "modulo of a multiple by a negative divisor");
    expect_out(G, "print minus 5\nprint minus -5\nprint minus 2 + 3\n", "-5\n5\n-5\n", "minus");
    expect_out(G, "make \"x 4\nprint -:x\nprint 3 * -:x\nprint -:x * -:x\n", "-4\n-12\n16\n",
               "unary minus");
    expect_out(G, "make \"x 4\nprint -(-:x)\nprint 2 * -(-3)\nprint -(sum 1 2)\n", "4\n6\n-3\n",
               "minus of a parenthesized expression");
    expect_out(G, "print 5 - -3\n", "8\n", "a negative literal after an operator");
    expect_out(G, "print 3-1\n", "2\n", "minus without spaces subtracts");
    expect_out(G, "print -3 + 1\n", "-2\n", "a leading negative literal");
    expect_out(G, "print 2 * -3 + 1\n", "-5\n", "a negative literal binds to its number");
    expect_out(G, "print (-3 + 1)\n", "-2\n", "a sign after a parenthesis");
    expect_out(G, "show list 3 -1\n", "[3 -1]\n", "\"a -b\" is two inputs");
    expect_out(G, "print 7 / 7\nprint -8 / 2\n", "1\n-4\n", "exact division");
    expect_out(G,
               "print 2 < 3\nprint 3 < 2\nprint 2 > 1\nprint 2 <= 2\nprint 2 >= 3\nprint 2 <> 3\n",
               "true\nfalse\ntrue\ntrue\nfalse\ntrue\n", "comparisons");
    expect_out(G, "print 1 + 1 = 2\n", "true\n", "comparison binds loosest");
    expect_out(G,
               "print lessp 1 2\nprint greaterp 1 2\nprint equalp 3 3\nprint notequalp 3 3\nprint "
               "lessequalp 3 3\nprint greaterequalp 2 3\n",
               "true\nfalse\ntrue\nfalse\ntrue\nfalse\n", "comparison procedures");
    expect_out(G, "print 123456789 * 1000000\n", "123456789000000\n", "large products");
    expect_out(G, "print and \"true \"false\nprint or \"true \"false\nprint not \"true\n",
               "false\ntrue\nfalse\n", "and or not");
    expect_out(G, "print (and \"true \"true \"true)\nprint (or \"false \"false \"true)\n",
               "true\ntrue\n", "variadic and or");
    expect_out(G, "print and 1 < 2 2 < 3\n", "true\n", "and of comparisons");
    expect_out(G, "print \"abc = \"abc\nprint \"abc = \"abd\n", "true\nfalse\n", "word equality");
    expect_out(G, "print [1 [2]] = [1 [2]]\nprint [1 2] = [1 3]\nprint [] = []\n",
               "true\nfalse\ntrue\n", "list equality");
    expect_out(G, "print 1 = \"a\nprint [1] = 1\nprint equalp [a] [a]\n", "false\nfalse\ntrue\n",
               "mixed equality");
    expect_out(G,
               "print numberp 3\nprint numberp \"a\nprint listp []\nprint listp 3\nprint wordp "
               "\"a\nprint wordp 3\nprint wordp [a]\n",
               "true\nfalse\ntrue\nfalse\ntrue\ntrue\nfalse\n", "type predicates");
    expect_out(G, "print emptyp []\nprint emptyp [1]\n", "true\nfalse\n", "emptyp");
    expect_out(
        G, "show first [1 2 3]\nshow butfirst [1 2 3]\nshow last [1 2 3]\nshow butlast [1 2 3]\n",
        "1\n[2 3]\n3\n[1 2]\n", "list parts");
    expect_out(G, "show bf [1]\nshow bl [1]\n", "[]\n[]\n", "bf and bl of one element");
    expect_out(G, "show fput 0 [1 2]\nshow lput 3 [1 2]\nshow fput [a] []\n",
               "[0 1 2]\n[1 2 3]\n[[a]]\n", "fput and lput");
    expect_out(G, "show list 1 2\nshow (list 1 2 3)\nshow (list)\nshow list [a] [b]\n",
               "[1 2]\n[1 2 3]\n[]\n[[a] [b]]\n", "list");
    expect_out(G, "show sentence [a b] [c]\nshow se 1 2\nshow (se [a] 1 [[b]])\nshow se [] []\n",
               "[a b c]\n[1 2]\n[a 1 [b]]\n[]\n", "sentence flattens one level");
    expect_out(G, "print item 2 [a b c]\nprint count [a b c]\nprint count []\n", "b\n3\n0\n",
               "item and count");
    expect_out(G, "print memberp 2 [1 2 3]\nprint memberp 5 [1 2 3]\nprint memberp [1] [[1] 2]\n",
               "true\nfalse\ntrue\n", "memberp");
    expect_out(G, "make \"l [1 2 3]\nmake \"m fput 0 :l\nshow :l\nshow :m\n",
               "[1 2 3]\n[0 1 2 3]\n", "lists are values");
    expect_out(G, "make \"l lput 4 [1 2 3]\nshow :l\nshow lput 5 :l\nshow :l\n",
               "[1 2 3 4]\n[1 2 3 4 5]\n[1 2 3 4]\n", "lput copies");
    expect_out(G, "show first [[a b] c]\nshow first first [[a b] c]\n", "[a b]\na\n",
               "nested first");
    expect_out(G, "show [1 -2 3]\n", "[1 -2 3]\n", "negative numbers in a literal list");
    expect_out(G, "repeat 3 [show [x]]\n", "[x]\n[x]\n[x]\n", "a literal list built once");
    expect_out(G, "make \"x 5\nprint :x\nprint thing \"x\n", "5\n5\n", "make and thing");
    expect_out(G, "make \"x 1\nmake \"x :x + 1\nprint :x\n", "2\n", "make reads before it writes");
    expect_out(G, "make \"X 3\nprint :X\n", "3\n", "upper-case names");
    expect_out(G, "MAKE \"x 3\nPRINT :x\n", "3\n", "upper-case primitives");
    expect_out(G, "make \"x [a b]\nshow :x\n", "[a b]\n", "a list in a variable");
    expect_out(G, "repeat 3 [type \"a]\nprint []\n", "aaa\n", "repeat");
    expect_out(G, "repeat 0 [print 1]\nprint 2\n", "2\n", "repeat zero times");
    expect_out(G, "repeat 3 [type repcount]\nprint []\n", "123\n", "repcount");
    expect_out(G, "repeat 2 [repeat 2 [type repcount] type \"x]\nprint []\n", "12x12x\n",
               "nested repcount");
    expect_out(G, "print repcount\n", "-1\n", "repcount outside a repeat");
    expect_out(G, "to f\noutput repcount\nend\nrepeat 2 [type f]\nprint []\n", "12\n",
               "repcount is dynamic");
    expect_out(G, "if 1 < 2 [print \"yes]\nif 2 < 1 [print \"no]\n", "yes\n", "if");
    expect_out(G, "ifelse 1 < 2 [print \"a] [print \"b]\nifelse 2 < 1 [print \"a] [print \"b]\n",
               "a\nb\n", "ifelse");
    expect_out(G, "print ifelse 1 < 2 [3] [4]\n", "3\n", "ifelse as an operation");
    expect_out(G, "print (ifelse \"false [3] [4]) + 1\n", "5\n", "ifelse in parentheses");
    expect_out(G, "for [i 1 3] [type :i]\nprint []\n", "123\n", "for");
    expect_out(G, "for [i 3 1] [type :i]\nprint []\n", "321\n", "for counts down");
    expect_out(G, "for [i 1 10 3] [type :i]\nprint []\n", "14710\n", "for with a step");
    expect_out(G, "for [i 10 1 -4] [type :i]\nprint []\n", "1062\n", "for with a negative step");
    expect_out(G, "for [i 1 3 -1] [type :i]\nprint \"x\n", "x\n", "for that runs zero times");
    expect_out(G, "make \"i 7\nfor [i 1 2] [type :i]\nprint :i\n", "127\n",
               "the for variable is local");
    expect_out(G, "make \"a 2\nmake \"b 4\nfor [i :a :b] [type :i]\nprint []\n", "234\n",
               "for limits from variables");
    expect_out(G, "make \"n 0\nwhile [:n < 3] [make \"n :n + 1]\nprint :n\n", "3\n", "while");
    expect_out(G, "make \"n 0\nuntil [:n >= 3] [make \"n :n + 1]\nprint :n\n", "3\n", "until");
    expect_out(G, "while [\"false] [print 1]\nprint 2\n", "2\n", "while that never runs");
    expect_out(G, "make \"n 0\nrepeat 3 [\n  make \"n :n + 1\n  type :n\n]\nprint []\n", "123\n",
               "a list over several lines");
    expect_out(G, "to sq :x\noutput :x * :x\nend\nprint sq 7\n", "49\n", "an operation");
    expect_out(G, "to hi\nprint \"hi\nend\nhi\nhi\n", "hi\nhi\n", "a command");
    expect_out(G, "to f :a :b\nop :a - :b\nend\nprint f 10 3\n", "7\n", "op and two inputs");
    expect_out(G, "to fact :n\nif :n < 2 [output 1]\noutput :n * fact :n - 1\nend\nprint fact 10\n",
               "3628800\n", "recursion");
    expect_out(G, "to f :n\nif :n = 0 [stop]\ntype :n\nf :n - 1\nend\nf 3\nprint []\n", "321\n",
               "stop");
    expect_out(
        G, "to f\nlocal \"x\nmake \"x 5\ng\nend\nto g\nprint :x\nend\nmake \"x 1\nf\nprint :x\n",
        "5\n1\n", "dynamic scope sees the caller's local");
    expect_out(G, "to f :x\ng\nend\nto g\nprint :x\nend\nf 9\n", "9\n",
               "dynamic scope sees the caller's input");
    expect_out(G, "to f :x\nmake \"x :x + 1\nprint :x\nend\nmake \"x 1\nf 5\nprint :x\n", "6\n1\n",
               "an input is restored on exit");
    expect_out(G, "to f\nmake \"y 3\nend\nf\nprint :y\n", "3\n", "make without a local is global");
    expect_out(G, "to f\nrepeat 3 [if repcount = 2 [output 20]]\noutput 0\nend\nprint f\n", "20\n",
               "output inside a repeat");
    expect_out(G, "to f\nrepeat 3 [type repcount if repcount = 2 [stop]]\nprint \"x\nend\nf\n",
               "12x\n", "stop inside a repeat ends the repeat");
    expect_out(G, "to f\nfor [i 1 5] [if :i = 3 [output :i * 10]]\noutput 0\nend\nprint f\n",
               "30\n", "output inside a for");
    expect_out(G,
               "to f :n\nlocal \"k\nmake \"k 0\nwhile [:n > 0] [make \"n :n - 1 make \"k :k + "
               "2]\noutput :k\nend\nprint f 4\n",
               "8\n", "while inside a procedure");
    expect_out(G,
               "to g :l\nif emptyp :l [output []]\noutput fput 2 * first :l g bf :l\nend\nshow g "
               "[1 2 3]\n",
               "[2 4 6]\n", "list recursion");
    expect_out(G,
               "to ev :n\nif :n = 0 [output \"true]\noutput od :n - 1\nend\nto od :n\nif :n = 0 "
               "[output \"false]\noutput ev :n - 1\nend\nprint ev 10\n",
               "true\n", "mutual recursion");
    expect_out(G, "to f :a :b :c\noutput (list :c :b :a)\nend\nshow f 1 2 3\n", "[3 2 1]\n",
               "three inputs");
    expect_out(G, "to f\noutput 1\nend\nprint f + f\n", "2\n", "a procedure with no inputs");
    expect_out(G, "to f :x\noutput :x + 1\nend\nprint f 1 + f 2\n", "5\n",
               "a prefix input extends to the right");
    expect_out(G, "to f :x :y\noutput :x * 10 + :y\nend\nshow (list 1 + f 2 3 4)\n", "[24 4]\n",
               "a procedure call in a parenthesized list");
    expect_out(G, "to f\nprint 1\nend\n\n; comment\nf ; call\n", "1\n", "comments and blank lines");
    expect_out(G, "to f :n\nif :n = 0 [output 0]\noutput 1 + f :n - 1\nend\nprint f 100\n", "100\n",
               "recursion depth 100");
    expect_out(G, "make \"x 1\nto f\nprint :x\nend\nf\n", "1\n",
               "a procedure defined after top-level code");
    expect_out(G, "to f\ng\nend\nto g\nprint 4\nend\nf\n", "4\n",
               "a body may call a later procedure");
    expect_out(G, "to f :n\nprint :n\nend\nf 1 + 2\n", "3\n", "a command with an expression input");
    expect_out(G, "print 1 print 2\n", "1\n2\n", "two instructions on one line");
    expect_out(G, "to f :x\ntype :x\noutput :x\nend\n(print f 1 f 2)\n", "121 2\n",
               "print computes every input first");
    expect_out(G, "to f :x\ntype :x\noutput :x\nend\nshow list f 1 f 2\n", "12[1 2]\n",
               "inputs left to right");
    expect_trap(G, "print :nope\n", ZL_E_DOMAIN, 1, "a variable without a value");
    expect_trap(G, "print 1 + \"a\n", ZL_E_DOMAIN, 1, "arithmetic on a word");
    expect_trap(G, "print 1 + [1]\n", ZL_E_DOMAIN, 1, "arithmetic on a list");
    expect_trap(G, "print first []\n", ZL_E_DOMAIN, 1, "first of the empty list");
    expect_trap(G, "print bf []\n", ZL_E_DOMAIN, 1, "butfirst of the empty list");
    expect_trap(G, "print last []\n", ZL_E_DOMAIN, 1, "last of the empty list");
    expect_trap(G, "print bl []\n", ZL_E_DOMAIN, 1, "butlast of the empty list");
    expect_trap(G, "print first \"abc\n", ZL_E_DOMAIN, 1, "first of a word");
    expect_trap(G, "print count \"abc\n", ZL_E_DOMAIN, 1, "count of a word");
    expect_trap(G, "print item 4 [1 2 3]\n", ZL_E_BOUNDS, 1, "item past the end");
    expect_trap(G, "print item 0 [1 2 3]\n", ZL_E_BOUNDS, 1, "item zero");
    expect_trap(G, "print 1 / 0\n", ZL_E_DIVZERO, 1, "division by zero");
    expect_trap(G, "print 7 / 2\n", ZL_E_INEXACT, 1, "inexact division");
    expect_trap(G, "print quotient 7 2\n", ZL_E_INEXACT, 1, "inexact quotient");
    expect_trap(G, "print remainder 1 0\n", ZL_E_DIVZERO, 1, "remainder by zero");
    expect_trap(G, "print modulo 1 0\n", ZL_E_DIVZERO, 1, "modulo by zero");
    expect_trap(G, "if 1 [print 2]\n", ZL_E_DOMAIN, 1, "if of a number");
    expect_trap(G, "print not 0\n", ZL_E_DOMAIN, 1, "not of a number");
    expect_trap(G, "print and \"true 1\n", ZL_E_DOMAIN, 1, "and of a number");
    expect_trap(G, "repeat \"a [print 1]\n", ZL_E_DOMAIN, 1, "repeat of a word");
    expect_trap(G, "print 1\nrepeat -2 [print 1]\n", ZL_E_DOMAIN, 2, "repeat of a negative count");
    expect_trap(G, "print \"a < \"b\n", ZL_E_DOMAIN, 1, "ordering of words");
    expect_trap(G, "to f\nif \"false [output 1]\nend\nprint f\n", ZL_E_DOMAIN, 3,
                "an operation that outputs nothing");
    expect_trap(G, "to f :n\noutput f :n + 1\nend\nprint f 1\n", ZL_E_RSTACK, 0,
                "unbounded recursion");
    expect_trap(G, "to f :n\noutput 1 + f :n + 1\nend\nprint f 1\n", ZL_E_STACK, 0,
                "deep non-tail recursion");
    expect_trap(G, "make \"x 1\nrepeat 70 [make \"x :x * 2]\n", ZL_E_OVERFLOW, 2, "overflow");
    expect_trap(G, "make \"l []\nrepeat 20000 [make \"l fput 1 :l]\n", ZL_E_HEAP, 2,
                "list cells run out");
    expect_trap(G, "print 1\nprint 2 + \"x\n", ZL_E_DOMAIN, 2, "the trap line");
    expect_refused(G, "print 1.5\n", ZL_E_UNSUPPORTED, 1, "a decimal");
    expect_refused(G, "print 1e3\n", ZL_E_UNSUPPORTED, 1, "an exponent");
    expect_refused(G, "print 1000000000000000\n", ZL_E_UNSUPPORTED, 1, "10^15");
    expect_refused(G, "print 007\n", ZL_E_UNSUPPORTED, 1, "leading zeros");
    expect_refused(G, "print \"12\n", ZL_E_UNSUPPORTED, 1, "a quoted number");
    expect_refused(G, "fd 10\n", ZL_E_UNSUPPORTED, 1, "turtle graphics");
    expect_refused(G, "print random 10\n", ZL_E_UNSUPPORTED, 1, "random");
    expect_refused(G, "print word \"a \"b\n", ZL_E_UNSUPPORTED, 1, "word");
    expect_refused(G, "run [print 1]\n", ZL_E_UNSUPPORTED, 1, "run");
    expect_refused(G, "print sqrt 4\n", ZL_E_UNSUPPORTED, 1, "sqrt");
    expect_refused(G, "print abs -1\n", ZL_E_UNDEFINED, 1, "abs is not a UCBLogo primitive");
    expect_refused(G, "frobnicate 1\n", ZL_E_UNDEFINED, 1, "an unknown procedure");
    expect_refused(G, "print 1 +\n", ZL_E_SYNTAX, 1, "a missing operand");
    expect_refused(G, "print\n", ZL_E_SYNTAX, 1, "print without input");
    expect_refused(G, "print (1 + 2\n", ZL_E_SYNTAX, 1, "unclosed parenthesis");
    expect_refused(G, "print [1 2\n", ZL_E_SYNTAX, 1, "unclosed bracket");
    expect_refused(G, "print 1 ]\n", ZL_E_SYNTAX, 1, "stray bracket");
    expect_refused(G, "to f\nprint 1\n", ZL_E_SYNTAX, 1, "to without end");
    expect_refused(G, "to print\nend\n", ZL_E_SYNTAX, 1, "redefining a primitive");
    expect_refused(G, "to fd\nend\n", ZL_E_SYNTAX, 1, "redefining a primitive outside the subset");
    expect_refused(G, "to f\nend\nto f\nend\n", ZL_E_SYNTAX, 3, "defined twice");
    expect_refused(G, "to f :a :a\nend\n", ZL_E_SYNTAX, 1, "an input named twice");
    expect_refused(G, "to f [:x 1]\nend\n", ZL_E_UNSUPPORTED, 1, "optional inputs");
    expect_refused(G, "f\nto f\nend\n", ZL_E_UNDEFINED, 1, "called before its definition");
    expect_refused(G, "to f\noutput 1\nend\nf\n", ZL_E_SYNTAX, 4, "an operation used as a command");
    expect_refused(G, "to f\nend\nprint f\n", ZL_E_SYNTAX, 3, "a command used as an operation");
    expect_refused(G, "print 1 2\n", ZL_E_SYNTAX, 1, "a value nobody uses");
    expect_refused(G, "3\n", ZL_E_SYNTAX, 1, "an instruction that is a value");
    expect_refused(G, "stop\n", ZL_E_SYNTAX, 1, "stop at the top level");
    expect_refused(G, "output 1\n", ZL_E_SYNTAX, 1, "output at the top level");
    expect_refused(G, "to f\nprint 1\nlocal \"x\nend\n", ZL_E_UNSUPPORTED, 3,
                   "local after the start");
    expect_refused(G, "to f\nlocal \"x\nlocal \"x\nend\n", ZL_E_UNSUPPORTED, 3, "local twice");
    expect_refused(G, "local \"x\n", ZL_E_SYNTAX, 1, "local at the top level");
    expect_refused(G, "make \"a 1\nmake thing \"a 2\n", ZL_E_UNSUPPORTED, 2,
                   "make of a computed name");
    expect_refused(G, "print thing word \"a \"b\n", ZL_E_UNSUPPORTED, 1,
                   "thing of a computed name");
    expect_refused(G, "print if \"true [1]\n", ZL_E_UNSUPPORTED, 1, "if as an operation");
    expect_refused(G, "print ifelse \"true [1 2] [3]\n", ZL_E_UNSUPPORTED, 1,
                   "ifelse with two expressions");
    expect_refused(G, "print ifelse \"true [1] [2] + 1\n", ZL_E_SYNTAX, 1,
                   "an operator after ifelse");
    expect_refused(G, "print -sum 1 2\n", ZL_E_UNSUPPORTED, 1, "a sign before a call");
    expect_refused(G, "print --3\n", ZL_E_UNSUPPORTED, 1, "a sign on a sign");
    expect_refused(G, "print 2 - - 3\n", ZL_E_UNSUPPORTED, 1, "a sign with a space after it");
    expect_refused(G, "print 2*-3+1\n", ZL_E_UNSUPPORTED, 1, "a sign against an operator");
    expect_refused(G, "make \"n 3\nprint :n--1\n", ZL_E_UNSUPPORTED, 2, "two minus signs together");
    expect_refused(G, "print - 3\n", ZL_E_SYNTAX, 1, "a spaced sign at the start");
    expect_refused(G, "show (list 1 + sum 2 3 4)\n", ZL_E_UNSUPPORTED, 1,
                   "a greedy sum in a parenthesized call");
    expect_refused(G, "show (se 1 + product 2 3)\n", ZL_E_UNSUPPORTED, 1,
                   "a greedy product in a parenthesized call");
    expect_refused(G, "to f\nrepeat 2 [stop]\nend\n", ZL_E_UNSUPPORTED, 2,
                   "stop in a repeat that ends a procedure");
    expect_refused(G, "to f\nif \"true [repeat 2 [stop]]\nend\n", ZL_E_UNSUPPORTED, 2,
                   "stop in a tail repeat through if");
    expect_refused(
        G, "to f\nprint 1\nifelse \"true [print 2] [repeat 2 [print 3 stop]]\n; comment\n\nend\n",
        ZL_E_UNSUPPORTED, 3, "stop in a tail repeat through ifelse");
    expect_refused(G, "print \"Abc = \"abc\n", ZL_E_UNSUPPORTED, 1, "one word in two spellings");
    expect_refused(G, "print \"a.b\n", ZL_E_UNSUPPORTED, 1, "a word with punctuation");
    expect_refused(G, "print [a.b]\n", ZL_E_UNSUPPORTED, 1, "punctuation in a list");
    expect_refused(G, "print [1.5]\n", ZL_E_UNSUPPORTED, 1, "a decimal in a list");
    expect_refused(G, "to f\nend\nf 1\n", ZL_E_SYNTAX, 3, "too many inputs");
    expect_refused(G, "to f :x\nend\nf\n", ZL_E_SYNTAX, 3, "too few inputs");
    expect_refused(G, "print (sum 1 2\n", ZL_E_SYNTAX, 1, "unclosed variadic call");
    expect_refused(G, "(make \"a 1)\n", ZL_E_UNSUPPORTED, 1, "make in parentheses");
    expect_refused(G, "print first\n", ZL_E_SYNTAX, 1, "first without an input");
    expect_refused(G, "to\nend\n", ZL_E_SYNTAX, 1, "to without a name");
    expect_refused(G, "end\n", ZL_E_SYNTAX, 1, "end without to");
    expect_refused(G, "print \"\n", ZL_E_UNSUPPORTED, 1, "the empty word");
    expect_refused(G, "print :\n", ZL_E_SYNTAX, 1, "a colon without a name");
    expect_refused(G, "print 1 = = 2\n", ZL_E_SYNTAX, 1, "two operators");
    expect_twin(G, "PR 1+2\n", "print 1 + 2\n");
    expect_twin(G, "make \"X 3 pr :X\n", "make \"x 3\nprint :x\n");
    expect_twin(G, "make \"x 1\nprint thing \"x\n", "make \"x 1\nprint :x\n");
    expect_twin(G, "show se [a] bf [b c]\n", "show sentence [a] butfirst [b c]\n");
    expect_twin(G, "print (sum 1 2)\n", "print sum 1 2\n");
    expect_twin(G, "print (sum 1 2 3)\n", "print (sum 1 2 3)\n");
    expect_twin(G, "print sum (1 + 2) 3\n", "print sum 1 + 2 3\n");
    expect_twin(G, "print (sum 1 2) + 3\n", "print (sum 1 2) + 3\n");
    expect_twin(G, "print 1 + (sum 2 3)\n", "print 1 + sum 2 3\n");
    expect_twin(G, "print (1 + 2) * 3\n", "print (1 + 2) * 3\n");
    expect_twin(G, "print ((1 + 2))\n", "print 1 + 2\n");
    expect_twin(G, "print 1 - (2 - 3)\n", "print 1 - (2 - 3)\n");
    expect_twin(G, "print (1 - 2) - 3\n", "print 1 - 2 - 3\n");
    expect_twin(G, "print 2 * -3\n", "print 2 * -3\n");
    expect_twin(G, "print -(3)\n", "print -3\n");
    expect_twin(G, "print -(repcount)\n", "print -(repcount)\n");
    expect_twin(G, "print -(sum 1 2)\n", "print -(sum 1 2)\n");
    expect_twin(G, "show (list 1 2 3 + (sum 4 5))\n", "show (list 1 2 3 + (sum 4 5))\n");
    expect_twin(G, "show (list (1 + sum 2 3) 4)\n", "show list 1 + sum 2 3 4\n");
    expect_twin(G, "show (list (1 + sum 2 3) 4 5)\n", "show (list 1 + (sum 2 3) 4 5)\n");
    expect_twin(G, "print ifelse \"true [1] [2]\n", "print ifelse \"true [1] [2]\n");
    expect_twin(G, "print (ifelse \"true [1] [2]) + 1\n", "print (ifelse \"true [1] [2]) + 1\n");
    expect_twin(G, "to F :A\nOP :A\nEND\nPR F 1\n", "to f :a\n  output :a\nend\nprint f 1\n");
    expect_twin(G, "to f\n    print 1\n  print 2\nend\n", "to f\n  print 1\n  print 2\nend\n");
    expect_twin(G, "repeat 2 [\n  print 1\n]\n", "repeat 2 [print 1]\n");
    expect_twin(G, "print [ a  [b]  [] ]\n", "print [a [b] []]\n");
    expect_twin(G, "; hello\nprint 1   ;  note\n\n\nprint 2\n",
                "; hello\nprint 1 ; note\nprint 2\n");
    expect_twin(G, "if 1<2 [pr 1 pr 2]\n", "if 1 < 2 [print 1 print 2]\n");
    expect_twin(G, "for [I 1 3] [type :I]\n", "for [i 1 3] [type :i]\n");
    expect_twin(G, "(PRINT 1 2)\n", "(print 1 2)\n");
    expect_twin(G, "(show)\n", "(show)\n");
    expect_twin(G, "print bl bf [1 2 3]\n", "print butlast butfirst [1 2 3]\n");
    expect_twin(G, "make \"x 1 + 2 ; c\n", "make \"x 1 + 2 ; c\n");
    expect_twin(G, "print -7\n", "print -7\n");
    expect_twin(G, "show list 3 -1\n", "show list 3 -1\n");
    expect_twin(G, "while [1 > 2] [pr 1]\nuntil [\"true] []\n",
                "while [1 > 2] [print 1]\nuntil [\"true] []\n");
    /* nesting is bounded: expressions, lists and blocks */
    {
        static char big[4096];
        static const char *const open[] = {"print ", "print ", "show ", ""};
        static const char *const pre[] = {"(", "-(", "[", "repeat 1 ["};
        static const char *const post[] = {")", ")", "]", "]"};
        for (int k = 0; k < 4; k++) {
            size_t n = 0;
            n += (size_t) snprintf(big + n, sizeof big - n, "%s", open[k]);
            for (int i = 0; i < 70; i++)
                n += (size_t) snprintf(big + n, sizeof big - n, "%s", pre[k]);
            n += (size_t) snprintf(big + n, sizeof big - n, "%s", k == 3 ? "print 1" : "1");
            for (int i = 0; i < 70; i++)
                n += (size_t) snprintf(big + n, sizeof big - n, "%s", post[k]);
            snprintf(big + n, sizeof big - n, "\n");
            expect_refused(G, big, ZL_E_LIMIT, 1, "nesting limit");
        }
        /* a long chain of prefix calls nests too */
        size_t n = (size_t) snprintf(big, sizeof big, "print ");
        for (int i = 0; i < 200; i++) n += (size_t) snprintf(big + n, sizeof big - n, "minus ");
        snprintf(big + n, sizeof big - n, "1\n");
        expect_refused(G, big, ZL_E_LIMIT, 1, "call nesting limit");
        /* within the limits it compiles and runs */
        n = (size_t) snprintf(big, sizeof big, "print ");
        for (int i = 0; i < 20; i++) n += (size_t) snprintf(big + n, sizeof big - n, "minus ");
        snprintf(big + n, sizeof big - n, "1\n");
        expect_out(G, big, "1\n", "20 nested calls");
        /* too many procedures */
        n = 0;
        for (int i = 0; i < 140 && n < sizeof big - 32; i++)
            n += (size_t) snprintf(big + n, sizeof big - n, "to p%d\nend\n", i);
        expect_refused(G, big, ZL_E_LIMIT, 0, "too many procedures");
    }
    /* a compiled program is a 62-bit Logo program */
    {
        zl_diag_t d;
        CHECK(compile(G, "print 1\n", &d) == ZL_OK && P.word_bits == 62 && P.lang == G,
              "Logo programs are 62-bit");
        CHECK(compile(G, "", &d) == ZL_OK && run_p(&P, 0, 0, 0) == ZL_OK && out[0] == 0,
              "the empty program");
        CHECK(compile(G, "\n\n; only a comment\n", &d) == ZL_OK, "only a comment");
        CHECK(compile(G, "print 1", &d) == ZL_OK, "no newline at the end");
        CHECK(run_src(G, "repeat 100000 [make \"x 1]\n", 0, 0, 1000) == ZL_E_FUEL,
              "fuel stops a long loop");
    }
    /* zl_ir: OUTV traces the numbers it prints (in lists too), as OUTI does */
    {
        int64_t tr[8];
        zl_trace_t t = {tr, 8, 0};
        prog_reset(62);
        I(ZL_PUSH, 5, 0);
        I(ZL_OUTV, ZL_DIALECT_LOGO, 0);
        I(ZL_PUSH, ZL_V_TRUE, 0);
        I(ZL_OUTV, ZL_DIALECT_LOGO, 0);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_PUSH, -8, 0);
        I(ZL_SWAP, 0, 0);
        I(ZL_CONS, 0, 0);
        I(ZL_PUSH, 7, 0);
        I(ZL_SWAP, 0, 0);
        I(ZL_CONS, 0, 0);
        I(ZL_OUTV, ZL_DIALECT_LOGO_SHOW, 0);
        I(ZL_HALT, 0, 0);
        memset(&R, 0, sizeof R);
        R.out = out;
        R.out_cap = sizeof out;
        R.trace = &t;
        int s = zl_run(&P, &R);
        CHECK(s == ZL_OK && strcmp(out, "5true[7 -8]") == 0, "OUTV fixture: %d [%s]", s, out);
        CHECK(t.n == 3 && tr[0] == 5 && tr[1] == 7 && tr[2] == -8, "OUTV traces numbers (%zu)",
              t.n);
    }
    /* zl_ir: a trap in code without a line reports the line of its caller */
    {
        prog_reset(64);
        I(ZL_CALL, 3, 0); /* line 1 */
        I(ZL_HALT, 0, 0);
        I(ZL_HALT, 0, 0);
        I(ZL_CALL, 6, 0); /* line 4, a helper calling a helper */
        I(ZL_RET, 0, 0);
        I(ZL_HALT, 0, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_DIVT, 0, 0);
        I(ZL_RET, 0, 0);
        for (uint32_t i = 3; i < P.ncode; i++) P.code[i].line = 0;
        CHECK(run_p(&P, 0, 0, 0) == ZL_E_DIVZERO && R.trap_line == 1 && R.trap_pc == 8,
              "a helper's trap has its caller's line (%u)", R.trap_line);
        P.code[3].line = 4;
        CHECK(run_p(&P, 0, 0, 0) == ZL_E_DIVZERO && R.trap_line == 4,
              "the nearest caller with a line (%u)", R.trap_line);
        P.code[8].line = 9;
        CHECK(run_p(&P, 0, 0, 0) == ZL_E_DIVZERO && R.trap_line == 9, "its own line first");
    }
}
