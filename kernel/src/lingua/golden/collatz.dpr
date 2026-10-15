// The same three algorithms as collatz.f, collatz.bas and collatz.pas:
// Collatz steps for 1..12, Euclid's GCD, Fibonacci numbers.
program Collatz;
{$APPTYPE CONSOLE}

function CollatzSteps(K: Integer): Integer;
begin
  Result := 0;
  while K <> 1 do
  begin
    if Odd(K) then
      K := 3 * K + 1
    else
      K := K div 2;
    Inc(Result);
  end;
end;

function Gcd(X, Y: Integer): Integer;
begin
  if Y = 0 then
    Exit(X);
  Result := Gcd(Y, X mod Y); { recursion }
end;

var
  N, F1, F2, T, I: Integer;
begin
  for N := 1 to 12 do
    WriteLn(N:4, CollatzSteps(N):4);
  WriteLn('GCD ', Gcd(1071, 462));
  F1 := 0;
  F2 := 1;
  for I := 1 to 15 do
  begin
    WriteLn(F1);
    T := F1 + F2;
    F1 := F2;
    F2 := T;
  end;
end.
