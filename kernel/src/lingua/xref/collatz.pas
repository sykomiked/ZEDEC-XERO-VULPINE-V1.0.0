{ Cross-reference for golden/collatz.alg: the same numbers, in Pascal. }
program collatz;
var n, i, t, f1, f2: int64;

function steps(n: int64): int64;
var s: int64;
begin
  s := 0;
  while n <> 1 do
  begin
    if n mod 2 = 0 then n := n div 2 else n := 3 * n + 1;
    s := s + 1
  end;
  steps := s
end;

function gcd(a, b: int64): int64;
begin
  if b = 0 then gcd := a else gcd := gcd(b, a mod b)
end;

begin
  for n := 1 to 12 do
    writeln(n, ' ', steps(n));
  writeln('GCD ', gcd(1071, 462));
  f1 := 0;
  f2 := 1;
  for i := 1 to 15 do
  begin
    writeln(f1);
    t := f1 + f2;
    f1 := f2;
    f2 := t
  end
end.
