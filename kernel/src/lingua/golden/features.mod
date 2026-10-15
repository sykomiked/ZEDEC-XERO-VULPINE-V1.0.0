(* Modula-2 features of the Lingua subset: CONST, arrays with any bounds,
   nested procedures that reach their parent's variables, VAR parameters,
   RETURN from inside a loop, IF/ELSIF, LOOP and EXIT, REPEAT, FOR with BY,
   CASE with ranges and ELSE, INC and DEC, DIV and MOD that round down.
   (* Comments nest. *) *)
MODULE Features;
FROM InOut IMPORT WriteInt, WriteString, WriteLn;

CONST
  Lo = -3;
  Hi = 4;

VAR
  a: ARRAY [Lo..Hi] OF INTEGER;
  i, total: INTEGER;
  seen: BOOLEAN;

PROCEDURE Fill(VAR count: INTEGER);
VAR
  next: INTEGER;

  PROCEDURE Put(v: INTEGER);
  BEGIN
    a[next] := v;
    INC(next);
    INC(count)
  END Put;

BEGIN
  next := Lo;
  count := 0;
  REPEAT
    Put(next * next - 2)
  UNTIL next > Hi
END Fill;

PROCEDURE FirstAbove(limit: INTEGER): INTEGER;
VAR
  k: INTEGER;
BEGIN
  FOR k := Lo TO Hi DO
    IF a[k] > limit THEN
      RETURN k
    END
  END;
  RETURN MIN(INTEGER)
END FirstAbove;

PROCEDURE Kind(n: INTEGER): INTEGER;
VAR
  r: INTEGER;
BEGIN
  CASE n MOD 7 OF
    0: r := 0
  | 1..3: r := 1
  | 4, 6: r := 2
  ELSE
    r := 3
  END;
  RETURN r
END Kind;

BEGIN
  Fill(total);
  FOR i := Hi TO 0 BY -2 DO
    WriteInt(a[i], 4)
  END;
  FOR i := Lo TO -1 BY 2 DO
    WriteInt(a[i], 4)
  END;
  WriteLn;
  WriteString("count ");
  WriteInt(total, 0);
  WriteString(", first above 5 at ");
  WriteInt(FirstAbove(5), 0);
  WriteLn;
  i := 0;
  total := 0;
  LOOP
    INC(i, 3);
    IF i > 20 THEN
      EXIT
    ELSIF ODD(i) THEN
      DEC(total, Kind(i))
    ELSE
      INC(total, Kind(i))
    END
  END;
  WriteInt(i, 0);
  WriteInt(total, 4);
  WriteLn;
  WriteInt(-7 DIV 2, 3);
  WriteInt(-7 MOD 2, 3);
  WriteInt((-7) DIV 2, 3);
  WriteInt((-7) MOD 2, 3);
  WriteInt(7 DIV 2, 3);
  WriteInt(ABS(-9), 3);
  WriteInt(MAX(INTEGER), 12);
  WriteLn;
  seen := (total # 0) & NOT (i < 0) OR ~TRUE;
  IF seen AND (FirstAbove(100) = MIN(INTEGER)) THEN
    WriteString("none above 100")
  END;
  WriteLn
END Features.
