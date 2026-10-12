--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Hexagram is not an integer (use From_Tag, which checks 0 .. 63).
--  expect: expected private type .*Hexagram
with Zxv.Hexagrams; use Zxv.Hexagrams;
procedure Hexagram_From_Integer is
   H : Hexagram := 59;
begin
   null;
end Hexagram_From_Integer;
