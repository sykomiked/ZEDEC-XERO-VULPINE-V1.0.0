begin
  comment Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC;
  comment SPDX-License-Identifier: Apache-2.0;
  comment An amount in minor units at an exchange rate, in ALGOL 60 (twin Rootwyrm);
  comment floor(amount * rnum / rden), whole minor units of the other currency (tally_rules.h TL3);
  comment amount 0..999999999999, rnum and rden 1..999999 (Sutra's pictures);
  comment anything outside is refused with words, which is no number (zxapp_lingua.c L2);
  comment a Lingua block reads one row per call (zxapp_lingua.c L1);
  integer amount, rnum, rden;
  ininteger(0, amount);
  ininteger(0, rnum);
  ininteger(0, rden);
  if amount < 0 | amount > 999999999999 | rnum < 1 | rnum > 999999 | rden < 1 | rden > 999999 then
    outstring(1, "refused")
  else
    outinteger(1, amount * rnum % rden)
end
