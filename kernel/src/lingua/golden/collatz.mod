(* The same three algorithms as collatz.f, collatz.bas and collatz.pas:
   Collatz steps for 1..12, Euclid's GCD, Fibonacci numbers. *)
MODULE Collatz;
FROM InOut IMPORT WriteInt, WriteString, WriteLn;
VAR
  n, f1, f2, t, i: INTEGER;

PROCEDURE CollatzSteps(k: INTEGER): INTEGER;
VAR
  s: INTEGER;
BEGIN
  s := 0;
  WHILE k # 1 DO
    IF ODD(k) THEN
      k := 3 * k + 1
    ELSE
      k := k DIV 2
    END;
    INC(s)
  END;
  RETURN s
END CollatzSteps;

PROCEDURE Gcd(x, y: INTEGER): INTEGER;
BEGIN
  IF y = 0 THEN
    RETURN x
  END;
  RETURN Gcd(y, x MOD y) (* recursion *)
END Gcd;

BEGIN
  FOR n := 1 TO 12 DO
    WriteInt(n, 4);
    WriteInt(CollatzSteps(n), 4);
    WriteLn
  END;
  WriteString("GCD ");
  WriteInt(Gcd(1071, 462), 0);
  WriteLn;
  f1 := 0;
  f2 := 1;
  FOR i := 1 TO 15 DO
    WriteInt(f1, 0);
    WriteLn;
    t := f1 + f2;
    f1 := f2;
    f2 := t
  END
END Collatz.
