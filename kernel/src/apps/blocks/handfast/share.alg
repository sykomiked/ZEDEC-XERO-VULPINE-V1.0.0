begin
  comment Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC;
  comment SPDX-License-Identifier: Apache-2.0;
  comment One party's obligation on one due date, in ALGOL 60 (twin Rootwyrm), handfast_rules.h HR4;
  comment floor(amount / obligors), plus one minor unit when pos is at most amount mod obligors;
  comment amount 0..999999999999999, obligors 1..99 and pos 1..obligors (Sutra's pictures);
  comment anything outside is refused with words, which is no number (zxapp_lingua.c L2);
  comment a Lingua block reads one row per call (zxapp_lingua.c L1);
  integer amount, obligors, pos, base;
  ininteger(0, amount);
  ininteger(0, obligors);
  ininteger(0, pos);
  if amount < 0 | amount > 999999999999999 | obligors < 1 | obligors > 99 | pos < 1 | pos > obligors then
    outstring(1, "refused")
  else begin
    base := amount % obligors;
    if pos <= amount - base * obligors then
      outinteger(1, base + 1)
    else
      outinteger(1, base)
  end
end
