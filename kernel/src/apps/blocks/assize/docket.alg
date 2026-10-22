begin
  comment Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC;
  comment SPDX-License-Identifier: Apache-2.0;
  comment A filing's docket number, in ALGOL 60 (twin Rootwyrm), assize_rules.h AR3;
  comment caseno * 10000 + seq, caseno 1..999999 and seq 1..9999, the proof of its domain;
  comment no docket number names case 0 or filing 0, so either is refused here;
  comment Sutra's pictures hold them, so the two are held;
  comment anything outside is refused with words, which is no number (zxapp_lingua.c L2);
  comment a Lingua block reads one row per call (zxapp_lingua.c L1);
  integer caseno, seq;
  ininteger(0, caseno);
  ininteger(0, seq);
  if caseno < 1 | caseno > 999999 | seq < 1 | seq > 9999 then
    outstring(1, "refused")
  else
    outinteger(1, caseno * 10000 + seq)
end
