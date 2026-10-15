{$mode iso}
{ Block structure: nested procedures that reach their parents' locals,
  var parameters, recursion (Hanoi, Ackermann, a recursive sum over an
  array), and a sort through a procedure that swaps by reference. }
program Nesting(input, output);
const
  N = 8;
var
  data: array[1..N] of integer;
  moves, k, total: integer;

procedure Swap(var x, y: integer);
var t: integer;
begin
  t := x; x := y; y := t
end;

procedure Hanoi(disks, src, dst, via: integer; var count: integer);
begin
  if disks > 0 then
  begin
    Hanoi(disks - 1, src, via, dst, count);
    count := count + 1;
    if disks = 3 then writeln('  move disk 3 from ', src:1, ' to ', dst:1);
    Hanoi(disks - 1, via, dst, src, count)
  end
end;

function Ackermann(m, n: integer): integer;
begin
  if m = 0 then Ackermann := n + 1
  else if n = 0 then Ackermann := Ackermann(m - 1, 1)
  else Ackermann := Ackermann(m - 1, Ackermann(m, n - 1))
end;

procedure SortAndReport;
var
  passes: integer;
  scratch: array[-1..1] of integer;

  procedure Pass(var changed: boolean);
  var i: integer;
  begin
    changed := false;
    for i := 1 to N - 1 do
      if data[i] > data[i + 1] then
      begin
        Swap(data[i], data[i + 1]);
        changed := true
      end;
    passes := passes + 1 { the parent's local, one static link out }
  end;

  procedure Sort;
  var again: boolean;
  begin
    repeat
      Pass(again)
    until not again
  end;

begin
  passes := 0;
  Sort;
  scratch[-1] := data[1];
  scratch[0] := data[N div 2];
  scratch[1] := data[N];
  writeln('sorted in ', passes:1, ' passes: min ', scratch[-1]:1, ' mid ', scratch[0]:1,
          ' max ', scratch[1]:1)
end;

function SumFrom(i: integer): integer;
begin
  if i > N then SumFrom := 0
  else SumFrom := data[i] + SumFrom(i + 1)
end;

function Depth(level: integer): integer;
  function Inner(x: integer): integer;
    function Innermost(y: integer): integer;
    begin
      Innermost := y * 100 + level  { two static links out }
    end;
  begin
    Inner := Innermost(x + 1) + level * 10
  end;
begin
  Depth := Inner(level)
end;

begin
  moves := 0;
  Hanoi(5, 1, 3, 2, moves);
  writeln('hanoi 5: ', moves:1, ' moves');
  writeln('ackermann(2, 3) = ', Ackermann(2, 3):1);
  writeln('ackermann(3, 3) = ', Ackermann(3, 3):1);
  for k := 1 to N do data[k] := (k * 37 + 11) mod 23 - 7;
  for k := 1 to N do write(data[k]:4);
  writeln;
  total := SumFrom(1);
  SortAndReport;
  for k := 1 to N do write(data[k]:4);
  writeln;
  writeln('sum ', total:1, ' = ', SumFrom(1):1);
  writeln(Depth(4), Depth(-2):6)
end.
