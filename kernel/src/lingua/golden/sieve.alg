begin
  comment The sieve of Eratosthenes. The Boolean array's size is read when
    its block is entered (a dynamic array), and the sieve runs twice;
  integer limit;

  procedure sieve(n); value n; integer n;
  begin
    Boolean array composite[2:n];
    integer i, j, count;
    for i := 2 step 1 until n do
      composite[i] := false;
    count := 0;
    for i := 2 step 1 until n do
      if !composite[i] then
      begin
        count := count + 1;
        outinteger(1, i);
        if count = count % 10 * 10 then
          outstring(1, "\n");
        for j := i * i step i until n do
          composite[j] := true
      end;
    if count != count % 10 * 10 then
      outstring(1, "\n");
    outinteger(1, count);
    outstring(1, "primes up to ");
    outinteger(1, n);
    outstring(1, "\n")
  end;

  for limit := 30, 100 do
    sieve(limit)
end
