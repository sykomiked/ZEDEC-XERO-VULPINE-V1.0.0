{$mode iso}
{ The same three algorithms as collatz.f, collatz.bas and collatz.fs:
  Collatz steps for 1..12, Euclid's GCD, Fibonacci numbers. }
program Collatz(output);
var
  n, steps, a, b, f1, f2, t, i: integer;

function CollatzSteps(k: integer): integer;
var s: integer;
begin
  s := 0;
  while k <> 1 do
  begin
    if k mod 2 = 0 then k := k div 2
    else k := 3 * k + 1;
    s := s + 1
  end;
  CollatzSteps := s
end;

function Gcd(x, y: integer): integer;
begin
  if y = 0 then Gcd := x
  else Gcd := Gcd(y, x mod y)   (* recursion *)
end;

begin
  for n := 1 to 12 do
  begin
    steps := CollatzSteps(n);
    writeln(n, steps)
  end;
  a := 1071; b := 462;
  writeln('GCD', Gcd(a, b));
  f1 := 0; f2 := 1;
  for i := 1 to 15 do
  begin
    writeln(f1);
    t := f1 + f2;
    f1 := f2;
    f2 := t
  end
end.
