(* Modula-2 equivalent of golden/features.ob, for ref_oberon.sh: it must
   print the same integers in the same order. Oberon's FOR, which reads its
   limit every time round, is written as a WHILE; Oberon's WHILE with an
   ELSIF arm as a LOOP. *)
MODULE Features;
FROM InOut IMPORT WriteInt, WriteString, WriteLn;

CONST
  N = 8;

VAR
  a: ARRAY [0..7] OF INTEGER;
  i, j, total, limit: INTEGER;
  done: BOOLEAN;

PROCEDURE Fill(VAR count: INTEGER);
VAR
  k: INTEGER;
BEGIN
  count := 0;
  k := 0;
  REPEAT
    a[k] := k * k - 2 * k;
    INC(count);
    INC(k)
  UNTIL k = N
END Fill;

PROCEDURE Kind(n: INTEGER): INTEGER;
BEGIN
  CASE n MOD 7 OF
    0: RETURN 0
  | 1..3: RETURN 1
  | 4, 6: RETURN 2
  | 5: RETURN 3
  END
END Kind;

PROCEDURE Gcd(x, y: INTEGER): INTEGER;
BEGIN
  LOOP
    IF x > y THEN
      DEC(x, y)
    ELSIF y > x THEN
      DEC(y, x)
    ELSE
      EXIT
    END
  END;
  RETURN x
END Gcd;

BEGIN
  Fill(total);
  i := 7;
  WHILE i >= 0 DO
    WriteInt(a[i], 4);
    DEC(i, 3)
  END;
  WriteLn;
  WriteString("count ");
  WriteInt(total, 0);
  WriteString(", gcd ");
  WriteInt(Gcd(84, 36), 0);
  WriteLn;
  limit := 4;
  j := 0;
  i := 1;
  WHILE i <= limit DO
    INC(j);
    IF i = 2 THEN
      limit := 6
    END;
    INC(i)
  END;
  WriteInt(i, 0);
  WriteInt(j, 3);
  WriteLn;
  total := 0;
  i := 0;
  WHILE i < 20 DO
    INC(i, 3);
    IF ODD(i) THEN
      DEC(total, Kind(i))
    ELSIF i MOD 4 = 0 THEN
      INC(total, 10 * Kind(i))
    ELSE
      INC(total, Kind(i))
    END
  END;
  WriteInt(i, 0);
  WriteInt(total, 4);
  WriteLn;
  WriteInt(-7 DIV 2, 3);
  WriteInt((-7) DIV 2, 3);
  WriteInt((-7) MOD 2, 3);
  WriteInt(7 DIV 2, 3);
  WriteInt(ABS(-9), 3);
  WriteInt(MAX(INTEGER), 12);
  WriteLn;
  done := (total # 0) & ~(i < 0) OR FALSE;
  IF done THEN
    WriteString("done")
  END;
  WriteLn
END Features.
