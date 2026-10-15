{$mode iso}
{ Sieve of Eratosthenes over 2..500 with a Boolean array, then the
  primes in rows of ten, their count and the twin-prime pairs. }
program Primes(output);
const
  Limit = 500;
  PerRow = 10;
var
  composite: array[2..Limit] of boolean;
  i, j, count, pairs, last: integer;
begin
  for i := 2 to Limit do composite[i] := false;
  i := 2;
  while i * i <= Limit do
  begin
    if not composite[i] then
    begin
      j := i * i;
      while j <= Limit do
      begin
        composite[j] := true;
        j := j + i
      end
    end;
    i := i + 1
  end;
  count := 0;
  for i := 2 to Limit do
    if not composite[i] then
    begin
      write(i:5);
      count := count + 1;
      if count mod PerRow = 0 then writeln
    end;
  writeln;
  writeln('primes below ', Limit:1, ': ', count:1);
  pairs := 0;
  last := 2;
  for i := 3 to Limit do
    if not composite[i] then
    begin
      if i - last = 2 then pairs := pairs + 1;
      last := i
    end;
  writeln('twin pairs: ', pairs:1);
  writeln('is 497 prime? ', not composite[497]);
  writeln('is 499 prime? ', not composite[499])
end.
