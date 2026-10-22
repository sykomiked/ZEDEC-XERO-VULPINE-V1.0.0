begin
  comment Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC;
  comment SPDX-License-Identifier: Apache-2.0;
  comment A record's register number, in ALGOL 60 (twin Rootwyrm), silt_rules.h SR1;
  comment year * 1000000 + seq, year 0..9999 and seq 1..999999, the proof of its domain;
  comment no register number is 0, so seq 0 is refused here though Sutra's picture holds it;
  comment anything outside is refused with words, which is no number (zxapp_lingua.c L2);
  comment a Lingua block reads one row per call (zxapp_lingua.c L1);
  integer year, seq;
  ininteger(0, year);
  ininteger(0, seq);
  if year < 0 | year > 9999 | seq < 1 | seq > 999999 then
    outstring(1, "refused")
  else
    outinteger(1, year * 1000000 + seq)
end
