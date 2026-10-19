-- The same three algorithms as collatz.f, collatz.bas and collatz.fs:
-- Collatz steps for 1..12, Euclid's GCD, Fibonacci numbers.

local function steps(k)
  local s = 0
  while k ~= 1 do
    if k % 2 == 0 then k = k // 2 else k = 3 * k + 1 end
    s = s + 1
  end
  return s
end

function gcd(x, y)
  if y == 0 then return x end
  return gcd(y, x % y) -- recursion
end

for n = 1, 12 do
  print(n, steps(n))
end
print("GCD", gcd(1071, 462))
local f1, f2 = 0, 1
for i = 1, 15 do
  print(f1)
  f1, f2 = f2, f1 + f2
end
