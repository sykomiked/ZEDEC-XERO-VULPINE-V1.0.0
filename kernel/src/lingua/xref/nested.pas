{ Cross-reference for golden/nested.alg. Pascal has no call by name, so
  Jensen's device becomes a function parameter that reads the index
  variable, and the name-parameter swaps are written out step by step in
  the order the ALGOL thunks run. }
{$mode iso}
program nested(output);
var i, j, total, t: int64;
    a: array[1..10] of int64;

function sum(var k: int64; lo, hi: int64; function term: int64): int64;
var s: int64;
begin
  s := 0;
  k := lo;
  while k <= hi do
  begin
    s := s + term;
    k := k + 1
  end;
  sum := s
end;

function ai: int64; begin ai := a[i] end;
function ii: int64; begin ii := i end;
function ij: int64; begin ij := i * j end;
function inner: int64; begin inner := sum(j, 1, i, ij) end;
function evenai: int64;
begin
  if i mod 2 = 0 then evenai := a[i] else evenai := 0
end;

function ackermann(m, n: int64): int64;
begin
  if m = 0 then ackermann := n + 1
  else if n = 0 then ackermann := ackermann(m - 1, 1)
  else ackermann := ackermann(m - 1, ackermann(m, n - 1))
end;

function hanoi(n: int64): int64;
begin
  if n = 0 then hanoi := 0 else hanoi := 2 * hanoi(n - 1) + 1
end;

procedure counter(start: int64);
var c: int64;
  procedure tick(by: int64);
  begin
    c := c + by;
    total := total + c
  end;
begin
  c := start;
  tick(1);
  tick(2);
  tick(3);
  writeln(c, ' ', total)
end;

begin
  for i := 1 to 10 do a[i] := i * i;
  write(sum(i, 1, 10, ai), ' ');
  write(sum(i, 1, 10, ii), ' ');
  write(sum(i, 1, 4, inner), ' ');
  writeln(sum(i, 1, 10, evenai));
  i := 3;
  total := 4;
  t := i; i := total; total := t;
  write(i, ' ', total, ' ');
  t := a[1]; a[1] := a[10]; a[10] := t;
  write(a[1], ' ', a[10], ' ');
  i := 2;
  { swap(i, a[i]): t := i; i := a[i]; a[i] := t, indexed by the new i }
  t := i; i := a[i]; a[i] := t;
  writeln(i, ' ', a[2], ' ', a[4]);
  a[3] := a[3] + 5;
  total := total - 1;
  writeln(a[3], ' ', total, ' ', ackermann(2, 3), ' ', hanoi(10));
  total := 0;
  counter(10);
  counter(-5)
end.
