/* test_zl_lua.inc — Lua (Moonwyrm) front-end tests, included by test_zl.c.
 * Every accepted program's output is what lua 5.4 (and later) prints; the
 * twin round trips through the same IR. A trap that the reference also
 * raises (a nil value, division by zero, a zero step, input exhausted) is
 * unchecked; overflow is [ref differs]: Lua wraps, Lingua traps. */

static void test_lua_output(void)
{
    const int L = ZL_LANG_LUA;
    expect_out(L, "print(1, 2) print() print(-5, 0)",
               "1\t2\n\n-5\t0\n",
               "print separates by tabs and ends the line");
    expect_out(L, "io.write(1, \"a\", 22) io.write(\"\\n\")",
               "1a22\n",
               "io.write writes no separators");
    expect_out(L, "print(true, false, 1 < 2, 2 <= 1)",
               "true\tfalse\ttrue\tfalse\n",
               "booleans and comparisons print as Lua does");
    expect_out(L, "print(\"tab\\tq\\\"\\\\'n\\n\", 'it\\'s')",
               "tab\tq\"\\'n\n\tit's\n",
               "string escapes and both quote styles");
    expect_out(L, "print(\"a\" .. 1 .. \"b\", 2 .. 3, \"x\" .. -4)",
               "a1b\t23\tx-4\n",
               ".. joins numbers and strings");
    expect_out(L, "print(2 + 3 * 4, (2 + 3) * 4, 10 - 4 - 3, 2 * 3 % 4, 100 // 10 // 3)",
               "14\t20\t3\t2\t3\n",
               "precedence and left association");
    expect_out(L, "print(-7 // 2, -7 % 2, 7 // -2, 7 % -2, -7 // -2, -7 % -2, 0 % 5)",
               "-4\t1\t-4\t-1\t3\t-1\t0\n",
               "// and % are floored, as Lua's");
    expect_out(L, "print(1 | 2 & 3, 5 ~ 3 & 1, ~0, 6 & -2, 12 ~ 10, - -5, -(-5), ~~5)",
               "3\t4\t-1\t6\t6\t5\t5\t5\n",
               "bitwise operators and their precedence; unary minus twice");
    expect_out(L, "print(1 == 1, 1 ~= 2, true == true, true ~= false, 1 == true, false ~= 0)",
               "true\ttrue\ttrue\ttrue\tfalse\ttrue\n",
               "== and ~= between numbers and booleans");
    expect_out(L, "print(not true, not false, not 0, not not true, true and false or true, false or false)",
               "false\ttrue\tfalse\ttrue\ttrue\tfalse\n",
               "not, and, or on booleans; a number is true");
    expect_out(L, "local function f(n) io.write(\"[\", n, \"]\") return n end print(f(1) .. \"x\", f(2))",
               "[1][2]1x\t2\n",
               "print evaluates every argument before writing");
    expect_out(L, "local function g() io.write(\"g\") return true end print(g() and g(), false and g(), true or g())",
               "ggtrue\tfalse\ttrue\n",
               "and/or stop early");
    expect_out(L, "local z = 0 local b = z > 0 and 1 // z > 0 print(b)",
               "false\n",
               "and stops before a division by zero");
    expect_out(L, "if 1 > 2 then print(\"a\") elseif 2 > 3 then print(\"b\") elseif 3 > 2 then print(\"c\") else print(\"d\") end",
               "c\n",
               "if, elseif, else");
    expect_out(L, "local i = 0 while i < 3 do i = i + 1 io.write(i) end print()",
               "123\n",
               "while");
    expect_out(L, "for i = 1, 3 do for j = 1, 3 do if j > i then break end io.write(i, j, \" \") end end print()",
               "11 21 22 31 32 33 \n",
               "break leaves the innermost loop");
    expect_out(L, "local n = 0 repeat local m = n + 1 n = m until m >= 3 print(n)",
               "3\n",
               "repeat's condition sees the block's locals");
    expect_out(L, "for i = 3, 1, -1 do io.write(i) end for i = 1, 0 do io.write(\"x\") end for i = -3, -1 do io.write(i) end print()",
               "321-3-2-1\n",
               "numeric for: down, empty, negative");
    expect_out(L, "local s, e, st = 10, 1, -4 for i = s, e, st do io.write(i, \";\") end print()",
               "10;6;2;\n",
               "numeric for with a step held in a variable");
    expect_out(L, "for i = 1, 3 do local i = i * 10 io.write(i, \" \") end print()",
               "10 20 30 \n",
               "a local in a for body shadows the loop variable");
    expect_out(L, "local a, b = 1, 2 a, b = b, a print(a, b)",
               "2\t1\n",
               "assignment evaluates every value first");
    expect_out(L, "x = 5 x = x * 2 print(x)",
               "10\n",
               "globals");
    expect_out(L, "local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end print(fib(20))",
               "6765\n",
               "recursion");
    expect_out(L, "function isprime(n) if n < 2 then return false end local d = 2 while d * d <= n do if n % d == 0 then return false end d = d + 1 end return true end for k = 1, 20 do if isprime(k) then io.write(k, \" \") end end print()",
               "2 3 5 7 11 13 17 19 \n",
               "a function returning a boolean");
    expect_out(L, "function show(flag) if flag then print(\"on\") else print(\"off\") end end show(true) show(false)",
               "on\noff\n",
               "a boolean parameter");
    expect_out(L, "function hello() print(\"hi\") return end hello()",
               "hi\n",
               "a function that returns nothing");
    expect_out(L, "print(math.max(1), math.max(-1, -5), math.min(3, 2, 1), math.abs(0), math.abs(-9))",
               "1\t-1\t1\t0\t9\n",
               "math.max, math.min, math.abs");
    expect_out(L, "do local q = 1 print(q) end",
               "1\n",
               "do ... end scopes a local");
    expect_out(L, "if 0 then print(\"0 is true\") end while 0 do print(\"once\") break end",
               "0 is true\nonce\n",
               "a number as a condition is true");
}

static void test_lua_refused(void)
{
    const int L = ZL_LANG_LUA;
    expect_refused(L, "print(1/2)", ZL_E_UNSUPPORTED, 1, "/ gives a float");
    expect_refused(L, "print(2^3)", ZL_E_UNSUPPORTED, 1, "^ gives a float");
    expect_refused(L, "local x", ZL_E_UNSUPPORTED, 1, "a local without a value");
    expect_refused(L, "x = nil", ZL_E_UNSUPPORTED, 1, "nil");
    expect_refused(L, "local s = \"a\"", ZL_E_UNSUPPORTED, 1, "a string value");
    expect_refused(L, "print(#t)", ZL_E_UNSUPPORTED, 1, "#");
    expect_refused(L, "t = {}", ZL_E_UNSUPPORTED, 1, "a table");
    expect_refused(L, "print(1 << 2)", ZL_E_UNSUPPORTED, 1, "<<");
    expect_refused(L, "goto l", ZL_E_UNSUPPORTED, 1, "goto");
    expect_refused(L, "local f = function() end", ZL_E_UNSUPPORTED, 1, "an anonymous function");
    expect_refused(L, "for k, v in pairs(t) do end", ZL_E_UNSUPPORTED, 1, "the generic for");
    expect_refused(L, "print(undefined_thing)", ZL_E_UNDEFINED, 1, "a global read but never assigned");
    expect_refused(L, "nope(1)", ZL_E_UNDEFINED, 1, "a call to a function never defined");
    expect_refused(L, "print(1.5)", ZL_E_UNSUPPORTED, 1, "a float literal");
    expect_refused(L, "print(tostring(1))", ZL_E_UNSUPPORTED, 1, "a library function");
    expect_refused(L, "local b = true print(b + 1)", ZL_E_UNSUPPORTED, 1, "arithmetic on a boolean");
    expect_refused(L, "local x = 1 x = true", ZL_E_UNSUPPORTED, 1, "a name with two types");
    expect_refused(L, "function f(n) if n then return 1 end end print(f(true))", ZL_E_UNSUPPORTED, 1,
                   "a function that can end without a value");
    expect_refused(L, "function f() return f() end print(f())", ZL_E_UNSUPPORTED, 1,
                   "a type that never settles");
    expect_refused(L, "for i = 1, 3 do i = 2 end", ZL_E_UNSUPPORTED, 1, "assigning to a for variable");
    expect_refused(L, "print(1 and 2)", ZL_E_UNSUPPORTED, 1, "and on a number");
    expect_refused(L, "local x <const> = 1", ZL_E_UNSUPPORTED, 1, "local attributes");
    expect_refused(L, "print(\"a\" == \"b\")", ZL_E_UNSUPPORTED, 1, "a string comparison");
    expect_refused(L, "if x then end", ZL_E_UNDEFINED, 1, "a global condition never assigned");
    expect_refused(L, "print(0xffffffffffffffff)", ZL_E_UNSUPPORTED, 1, "a hex that wraps in Lua");
    expect_refused(L, "print(--[[x]] 1)", ZL_E_UNSUPPORTED, 1, "a long comment");
    expect_refused(L, "print([[x]])", ZL_E_UNSUPPORTED, 1, "a long string");
    expect_refused(L, "function f() function g() end end", ZL_E_UNSUPPORTED, 1, "a nested function");
    expect_refused(L, "function f() end function f() end", ZL_E_UNSUPPORTED, 1, "a function defined twice");
    expect_refused(L, "print(math.floor(1))", ZL_E_UNSUPPORTED, 1, "a library call outside the subset");
    expect_refused(L, "x = 1 x()", ZL_E_UNSUPPORTED, 1, "calling a variable");
    expect_refused(L, "function f() return 1, 2 end", ZL_E_UNSUPPORTED, 1, "a return of two values");
}

static void test_lua_trap(void)
{
    const int L = ZL_LANG_LUA;
    expect_trap(L, "print(1 // 0)", ZL_E_DIVZERO, 1, "// by zero");
    expect_trap(L, "print(7 % 0)", ZL_E_DIVZERO, 1, "% by zero");
    expect_trap(L, "function f() return g + 1 end print(f()) g = 1", ZL_E_DOMAIN, 1,
                "a global read before it is assigned");
    expect_trap(L, "h() function h() end", ZL_E_DOMAIN, 1, "a call before the definition ran");
    expect_trap(L, "local z = math.maxinteger print(z + 1)", ZL_E_OVERFLOW, 1, "[ref differs] overflow");
    expect_trap(L, "local z = math.mininteger print(-z)", ZL_E_OVERFLOW, 1, "[ref differs] -mininteger");
    expect_trap(L, "local s = 0 for i = 1, 3, s do end", ZL_E_DOMAIN, 1, "a zero for step");
    expect_trap(L, "print(io.read(\"n\"))", ZL_E_INPUT, 1, "input exhausted");
}

static void test_lua_twin(void)
{
    const int L = ZL_LANG_LUA;
    expect_twin(L, "x = 1 + 2", "x = 1 + 2\n");
    expect_twin(L, "print ( \"a\" , 1 )", "print(\"a\", 1)\n");
    expect_twin(L, "x=1;y=2", "x = 1\ny = 2\n");
    expect_twin(L, "print('x')", "print(\"x\")\n");
    expect_twin(L, "print(0xff)", "print(255)\n");
    expect_twin(L, "-- hi\nprint(1)", "-- hi\nprint(1)\n");
    expect_twin(L, "x = 1 -- c", "x = 1 -- c\n");
    expect_twin(L, "if 0 then print(1) end", "if 0 then\n  print(1)\nend\n");
}

static void test_lua(void)
{
    test_lua_output();
    test_lua_refused();
    test_lua_trap();
    test_lua_twin();
}