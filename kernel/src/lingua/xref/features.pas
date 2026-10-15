{ Cross-reference for golden/features.alg. ALGOL's % truncates like
  Pascal's div, ^ is an integer power, and the for list
  "1, 3, 5 step 5 until 20, i - 1 while i > 10" is unrolled. }
program features;
var i, j, k, n: int64;
    p, q: boolean;

function tri(n: int64): int64;
var t: array of int64;
    m: int64;
begin
  setlength(t, n + 1);
  t[0] := 0;
  for m := 1 to n do t[m] := t[m - 1] + m;
  if n > 3 then tri := t[n] + tri(n - 1) else tri := t[n]
end;

function b(x: boolean): int64;
begin
  if x then b := 1 else b := 0
end;

begin
  writeln(7 div 2, ' ', -(7 div 2), ' ', 7 div (-2), ' ', 1024, ' ', 64, ' ',
    -4, ' ', -8, ' ', 12 div 4, ' ', 1 - 2 - 3, ' ', 2 * (3 + 4), ' ',
    abs(-9), ' ', -1, ' ', 0, ' ', 81 div 9, ' ', abs(-4));
  i := 5; j := 5; k := 5;
  write(i + j + k, ' ');
  n := 1 + 3;
  i := 5;
  while i <= 20 do begin n := n + i; i := i + 5 end;
  i := i - 1;
  while i > 10 do begin n := n + i; i := i - 1 end;
  write(n, ' ');
  n := 0;
  i := 10;
  while i >= 0 do begin n := n + i; i := i - 3 end;
  writeln(n, ' ', i);
  for j := 0 to 1 do
    for k := 0 to 1 do
    begin
      p := j = 1;
      q := k = 1;
      write(b(p and q), ' ', b(p or q), ' ', b(not p or q), ' ', b(p = q), ' ', b(not p), ' ')
    end;
  writeln;
  i := 0;
  repeat i := i + 1 until not ((i < 10) and (i <> 7));
  write(i, ' ');
  if i > 5 then if i > 6 then write(3, ' ') else write(2, ' ') else write(1, ' ');
  writeln(tri(6))
end.
