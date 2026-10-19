-- features.lua: the Lua subset Lingua runs, checked against lua 5.4+.
local a, b = 7, -3
print(a // b, a % b, -a // 2, -a % 2, a * b - 1)  -- floored, as Lua
print(a & 6, a | 8, a ~ 5, ~a, 0xff, 0x7fffffffffffffff)
print(math.max(3, 9, -2), math.min(4, -1), math.abs(-8))
print(math.maxinteger, math.mininteger)
io.write('single', " and double", "\n")
io.write(1, " ", 2 + 3, "\t", "tab\n")
print("x" .. 3 + 4 .. "y", 1 .. 2)

-- booleans, and/or/not, a number is true
local t, f = true, false
print(t, f, not t, t and f, t or f, not 0, 1 == 1, 1 ~= 1, a == t)
if 0 then print("zero is true") end

function even(n)
  if n == 0 then return true end
  return odd(n - 1)
end
function odd(n)
  if n == 0 then return false end
  return even(n - 1)
end
print(even(10), odd(7), even(3))

-- loops: steps up, down, dynamic, empty, break, repeat sees its locals
for i = 10, 1, -3 do io.write(i, " ") end
print()
local step = 2
for i = 1, 9, step do io.write(i, ",") end
print()
for i = 5, 1 do print("never") end
for i = math.maxinteger - 2, math.maxinteger do io.write(i - math.maxinteger, " ") end
print()
local n = 0
while true do
  n = n + 1
  if n > 4 then break end
end
print("n", n)
local k = 0
repeat
  local sq = k * k
  k = k + 1
until sq >= 20
print("k", k)

-- globals and swaps
total = 0
for i = 1, 100 do total = total + i end
x, y = 1, 2
x, y = y, x
print(total, x, y)

do
  local a = 100   -- a shadows the outer a
  print(a)
end
print(a)

local function fact(m)
  if m <= 1 then
    return 1
  elseif m == 2 then
    return 2
  else
    return m * fact(m - 1)
  end
end
print(fact(20))
