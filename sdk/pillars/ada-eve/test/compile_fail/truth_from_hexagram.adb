--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Hexagram is not a Truth (convert the base with To_Truth).
--  expect: expected type .*Truth
with Zxv.Logic; use Zxv.Logic;
with Zxv.Hexagrams; use Zxv.Hexagrams;
procedure Truth_From_Hexagram is
   H : constant Hexagram := From_Tag (63);
   T : Truth := H;
begin
   null;
end Truth_From_Hexagram;
