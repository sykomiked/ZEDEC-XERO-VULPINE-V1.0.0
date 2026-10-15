{$mode iso}
(* One of each statement and operator in the subset, written the way
   people write Pascal: mixed case, compact lines, comments in both forms. *)
PROGRAM Features (OUTPUT);
CONST
  Ten = 10; MinusTen = -Ten; Yes = TRUE;
VAR
  i, j, x, zero : INTEGER;
  flag : BOOLEAN;
  sq : ARRAY [-3 .. 3] OF INTEGER;
BEGIN
  writeln('div and mod: ', -7 div 2, 7 div 2, (-7) mod 2, 7 mod 3, -(7 mod 3));
  writeln('precedence: ', 2 + 3 * 4, (2 + 3) * 4, -2 * 3 + 1, 10 - 4 - 3);
  writeln('functions: ', abs(MinusTen), sqr(-12), succ(Ten), pred(-Ten), ord(Yes), maxint);
  writeln('odd: ', odd(7), odd(-3), odd(0):7);
  writeln('widths:', 42:6, -42:6, 7:0, 'pad':6, 'cut':2, true:7, false:3, Yes);
  FOR i := -3 TO 3 DO sq[i] := i * i;
  FOR i := 3 DOWNTO -3 DO Write(sq[i]:3);
  WriteLn;
  x := 0;
  FOR i := 1 TO 5 DO
    FOR j := i TO 5 DO x := x + i * j;
  writeln('nested for: ', x);
  x := 1;
  REPEAT x := x * 3 UNTIL x > 1000;
  writeln('repeat: ', x);
  i := 0;
  WHILE (i < 100) AND NOT (i * i > 50) DO i := i + 1;
  writeln('while: ', i);
  FOR i := 0 TO 6 DO
    CASE i MOD 4 OF
      0: write('zero ');
      1, 3: write('odd ');
      2: BEGIN write('two'); write(' ') END;
    END;
  writeln;
  zero := 0;
  flag := (Ten > 5) OR (1 DIV zero = 0);  { or stops after a true left side }
  writeln('short circuit: ', flag, (Ten < 5) AND (1 MOD zero = 0));
  IF flag THEN
    IF Ten = 10 THEN writeln('dangling else binds inside')
    ELSE writeln('wrong')
  ELSE writeln('wrong too');
  writeln('comparisons: ', 1 < 2, 2 <= 1, 3 >= 3, 4 > 5, 5 <> 5, 6 = 6, FALSE < TRUE);
  writeln('limits: ', maxint - 1 + 1, -maxint);
END.
