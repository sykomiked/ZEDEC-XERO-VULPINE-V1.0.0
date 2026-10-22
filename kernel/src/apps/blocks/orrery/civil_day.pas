{ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC }
{ SPDX-License-Identifier: Apache-2.0 }
{ The day number of a Gregorian date (orrery_rules.h OR1), step by step:
  read the year, month and day, write the days since 1970-01-01 (a date
  before it is negative). Years 1 to 9999 only, and the day must be in its
  month: anything else is refused. An integer here is 32 bits. }
program civilday(input, output);
var
  y, m, d, last, ym, era, yoe, mp, doy, doe: integer;
begin
  read(y, m, d);
  { step 1: the date must exist }
  if (y < 1) or (y > 9999) or (m < 1) or (m > 12) then
    writeln('REFUSED')
  else
    begin
      last := 30 + (m + m div 8) mod 2;
      if m = 2 then
        begin
          last := 28;
          if (y mod 4 = 0) and (y mod 100 <> 0) or (y mod 400 = 0) then
            last := 29
        end;
      if (d < 1) or (d > last) then
        writeln('REFUSED')
      else
        begin
          { step 2: count the year from March, so the leap day ends it }
          ym := y;
          mp := m - 3;
          if m <= 2 then
            begin
              ym := y - 1;
              mp := m + 9
            end;
          { step 3: whole eras of 400 years (146097 days), then the rest }
          era := ym div 400;
          yoe := ym - era * 400;
          doy := (153 * mp + 2) div 5 + d - 1;
          doe := yoe * 365 + yoe div 4 - yoe div 100 + doy;
          { step 4: day 0 is 1970-01-01, which is 719468 days after 0000-03-01 }
          writeln(era * 146097 + doe - 719468)
        end
    end
end.
