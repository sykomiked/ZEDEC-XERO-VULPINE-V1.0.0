{ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC }
{ SPDX-License-Identifier: Apache-2.0 }
{ The flags a reader holds after taking a story choice (talewind_rules.h
  TR3), step by step: read the held flags and the flags the choice sets,
  each a mask of 16 flags (0 to 65535); every flag held before stays held,
  and every flag the choice sets is held after. A mask outside 0 to 65535
  is refused. }
program flagsafter(input, output);
var
  held, sets, k, h, s, weight, after: integer;
begin
  read(held, sets);
  { step 1: both masks must hold 16 flags or fewer }
  if (held < 0) or (held > 65535) or (sets < 0) or (sets > 65535) then
    writeln('REFUSED')
  else
    begin
      { step 2: start from what is held, then walk the 16 flags }
      after := held;
      h := held;
      s := sets;
      weight := 1;
      for k := 1 to 16 do
        begin
          { step 3: a flag the choice sets that was not held is added }
          if (s mod 2 = 1) and (h mod 2 = 0) then
            after := after + weight;
          h := h div 2;
          s := s div 2;
          weight := weight * 2
        end;
      writeln(after)
    end
end.
