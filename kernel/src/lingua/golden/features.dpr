// Delphi features of the Lingua subset: Result and Exit, Break and
// Continue, Inc and Dec, case ranges and else, bitwise operators on
// integers, nested procedures, var parameters and arrays.
program Features;
{$APPTYPE CONSOLE}
{$Q+}
{$R+}

const
  Size = 10;

var
  Primes: array[1..Size] of Integer;
  Count, I: Integer;

function IsPrime(N: Integer): Boolean;
var
  D: Integer;
begin
  Result := False;
  if N < 2 then
    Exit;
  D := 2;
  while D * D <= N do
  begin
    if N mod D = 0 then
      Exit(False);
    Inc(D);
  end;
  Result := True;
end;

procedure Fill(var Found: Integer);
var
  Candidate: Integer;

  procedure Keep(P: Integer);
  begin
    Inc(Found);
    Primes[Found] := P;
  end;

begin
  Found := 0;
  Candidate := 1;
  repeat
    Inc(Candidate);
    if not IsPrime(Candidate) then
      Continue;
    Keep(Candidate);
  until Found = Size;
end;

function Describe(N: Integer): Integer;
begin
  case N mod 7 of
    0: Result := 0;
    1..3: Result := 1;
    4, 6: Result := 2;
  else
    Result := 3;
  end;
end;

var
  Bits, Total: Integer;
  Done: Boolean;
begin
  Fill(Count);
  for I := 1 to Count do
    Write(Primes[I]:4);
  WriteLn;
  Total := 0;
  for I := 1 to Count do
  begin
    if Primes[I] > 20 then
      Break;
    Total := Total + Describe(Primes[I]);
  end;
  WriteLn('total ', Total);
  Bits := $F0 or 5;
  WriteLn(Bits, ' ', Bits and $3C, ' ', Bits xor -1, ' ', not Bits, ' ', -7 mod 3, ' ', -7 div 2);
  Done := (Total > 3) xor (Bits < 0);
  WriteLn(Done, ' ', not Done:6, '|', 'end':5);
  Dec(Total, 2 * -3);
  WriteLn(Total);
end.
