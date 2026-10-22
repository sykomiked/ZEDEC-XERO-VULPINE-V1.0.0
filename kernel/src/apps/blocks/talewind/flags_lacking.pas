{ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC }
{ SPDX-License-Identifier: Apache-2.0 }
{ How many flags a story choice needs that the reader does not hold yet
  (talewind_rules.h TR2), step by step: read the needed flags and the held
  flags, each a mask of 16 flags (0 to 65535), and count, flag by flag,
  the ones needed and not held. A mask outside 0 to 65535 is refused. }
program flagslacking(input, output);
var
  need, held, k, n, h, lacking: integer;
begin
  read(need, held);
  { step 1: both masks must hold 16 flags or fewer }
  if (need < 0) or (need > 65535) or (held < 0) or (held > 65535) then
    writeln('REFUSED')
  else
    begin
      { step 2: walk the 16 flags from the lowest, halving both masks }
      n := need;
      h := held;
      lacking := 0;
      for k := 1 to 16 do
        begin
          { step 3: this flag is needed and not held }
          if (n mod 2 = 1) and (h mod 2 = 0) then
            lacking := lacking + 1;
          n := n div 2;
          h := h div 2
        end;
      writeln(lacking)
    end
end.
