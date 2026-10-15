begin
  comment The same three algorithms as collatz.f, collatz.bas and collatz.fs:
    Collatz steps for 1 to 12, Euclid's greatest common divisor, Fibonacci
    numbers. In MARST's representation % is integer division;
  integer n, i, t, f1, f2;

  integer procedure steps(n); value n; integer n;
  begin
    integer s;
    s := 0;
    for n := n while n != 1 do
    begin
      n := if n - n % 2 * 2 = 0 then n % 2 else 3 * n + 1;
      s := s + 1
    end;
    steps := s
  end;

  integer procedure gcd(a, b); value a, b; integer a, b;
    gcd := if b = 0 then a else gcd(b, a - a % b * b);

  for n := 1 step 1 until 12 do
  begin
    outinteger(1, n);
    outinteger(1, steps(n));
    outstring(1, "\n")
  end;
  outstring(1, "GCD ");
  outinteger(1, gcd(1071, 462));
  outstring(1, "\n");
  f1 := 0;
  f2 := 1;
  for i := 1 step 1 until 15 do
  begin
    outinteger(1, f1);
    outstring(1, "\n");
    t := f1 + f2;
    f1 := f2;
    f2 := t
  end
end
