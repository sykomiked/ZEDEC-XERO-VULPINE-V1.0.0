{ Cross-reference for golden/sieve.alg: the same numbers, in Pascal. }
program sieve;
var limit: int64;

procedure run(n: int64);
var composite: array of boolean;
    i, j, count: int64;
begin
  setlength(composite, n + 1);
  for i := 2 to n do composite[i] := false;
  count := 0;
  for i := 2 to n do
    if not composite[i] then
    begin
      count := count + 1;
      write(i, ' ');
      if count mod 10 = 0 then writeln;
      j := i * i;
      while j <= n do
      begin
        composite[j] := true;
        j := j + i
      end
    end;
  if count mod 10 <> 0 then writeln;
  writeln(count, ' primes up to ', n)
end;

begin
  limit := 30;
  run(limit);
  limit := 100;
  run(limit)
end.
