begin
  comment Operators, conditional expressions, Boolean logic, for lists,
    multiple assignment, labels and a dynamic array in a recursive
    procedure;
  integer i, j, k, n;
  Boolean p, q;

  integer procedure tri(n); value n; integer n;
  begin
    integer array t[0:n];
    integer m;
    t[0] := 0;
    for m := 1 step 1 until n do
      t[m] := t[m - 1] + m;
    tri := if n > 3 then t[n] + tri(n - 1) else t[n]
  end;

  outinteger(1, 7 % 2);
  outinteger(1, -7 % 2);
  outinteger(1, 7 % (-2));
  outinteger(1, 2 ^ 10);
  outinteger(1, 2 ^ 3 ^ 2);
  outinteger(1, -2 ^ 2);
  outinteger(1, (-2) ^ 3);
  outinteger(1, 12 / 4);
  outinteger(1, 1 - 2 - 3);
  outinteger(1, 2 * (3 + 4));
  outinteger(1, iabs(-9));
  outinteger(1, sign(-9));
  outinteger(1, sign(0));
  outinteger(1, entier(81 / 9));
  outinteger(1, abs(-4));
  outstring(1, "\n");
  i := j := k := 5;
  outinteger(1, i + j + k);
  n := 0;
  for i := 1, 3, 5 step 5 until 20, i - 1 while i > 10 do
    n := n + i;
  outinteger(1, n);
  n := 0;
  for i := 10 step -3 until 0 do
    n := n + i;
  outinteger(1, n);
  outinteger(1, i);
  outstring(1, "\n");
  for j := 0, 1 do
    for k := 0, 1 do
    begin
      p := j = 1;
      q := k = 1;
      outinteger(1, if p & q then 1 else 0);
      outinteger(1, if p | q then 1 else 0);
      outinteger(1, if p -> q then 1 else 0);
      outinteger(1, if p == q then 1 else 0);
      outinteger(1, if !p then 1 else 0)
    end;
  outstring(1, "\n");
  i := 0;
again:
  i := i + 1;
  if i < 10 & i != 7 then
    goto again;
  outinteger(1, i);
  outinteger(1, if i > 5 then (if i > 6 then 3 else 2) else 1);
  outinteger(1, tri(6));
  outstring(1, "\n")
end
