begin
  comment Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC;
  comment SPDX-License-Identifier: Apache-2.0;
  comment Days until a version may go, in ALGOL 60 (twin Rootwyrm), cartulary_rules.h CR2;
  comment filed + floor(36525 * (12 * years + months) / 1200) - today, days since 1970;
  comment filed and today 0..999999, years 0..999, months 0..99 (Sutra's pictures);
  comment anything outside is refused with words, which is no number (zxapp_lingua.c L2);
  comment a Lingua block reads one row per call (zxapp_lingua.c L1);
  integer filed, years, months, today;
  ininteger(0, filed);
  ininteger(0, years);
  ininteger(0, months);
  ininteger(0, today);
  if filed < 0 | filed > 999999 | years < 0 | years > 999 | months < 0 | months > 99 | today < 0 | today > 999999 then
    outstring(1, "refused")
  else
    outinteger(1, filed + (12 * years + months) * 36525 % 1200 - today)
end
