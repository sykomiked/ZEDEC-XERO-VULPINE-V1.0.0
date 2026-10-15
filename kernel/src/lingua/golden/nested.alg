begin
  comment Nested procedures, recursion and call by name. A name parameter
    is evaluated again at every use, in the caller's scope: Jensen's
    device sums any expression of its index, and swap(i, a[i]) shows
    the classic surprise of call by name;
  integer i, j, total;
  integer array a[1:10];

  integer procedure sum(k, lo, hi, term); value lo, hi; integer k, lo, hi, term;
  begin
    integer s;
    s := 0;
    for k := lo step 1 until hi do
      s := s + term;
    sum := s
  end;

  procedure swap(x, y); integer x, y;
  begin
    integer t;
    t := x;
    x := y;
    y := t
  end;

  procedure bump(x, by); value by; integer x, by;
    x := x + by;

  integer procedure ackermann(m, n); value m, n; integer m, n;
    ackermann := if m = 0 then n + 1
      else if n = 0 then ackermann(m - 1, 1)
      else ackermann(m - 1, ackermann(m, n - 1));

  integer procedure hanoi(n); value n; integer n;
    hanoi := if n = 0 then 0 else 2 * hanoi(n - 1) + 1;

  procedure counter(start); value start; integer start;
  begin
    integer c;
    procedure tick(by); value by; integer by;
    begin
      c := c + by;
      total := total + c
    end;
    c := start;
    tick(1);
    tick(2);
    tick(3);
    outinteger(1, c);
    outinteger(1, total);
    outstring(1, "\n")
  end;

  Boolean procedure even(n); value n; integer n;
    even := n = n % 2 * 2;

  for i := 1 step 1 until 10 do
    a[i] := i * i;
  outinteger(1, sum(i, 1, 10, a[i]));
  outinteger(1, sum(i, 1, 10, i));
  outinteger(1, sum(i, 1, 4, sum(j, 1, i, i * j)));
  outinteger(1, sum(i, 1, 10, if even(i) then a[i] else 0));
  outstring(1, "\n");
  i := 3;
  total := 4;
  swap(i, total);
  outinteger(1, i);
  outinteger(1, total);
  swap(a[1], a[10]);
  outinteger(1, a[1]);
  outinteger(1, a[10]);
  i := 2;
  swap(i, a[i]);
  outinteger(1, i);
  outinteger(1, a[2]);
  outinteger(1, a[4]);
  outstring(1, "\n");
  bump(a[3], 5);
  bump(total, -1);
  outinteger(1, a[3]);
  outinteger(1, total);
  outinteger(1, ackermann(2, 3));
  outinteger(1, hanoi(10));
  outstring(1, "\n");
  total := 0;
  counter(10);
  counter(-5)
end
