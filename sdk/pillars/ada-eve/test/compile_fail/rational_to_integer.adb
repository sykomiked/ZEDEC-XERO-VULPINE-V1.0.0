--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Rational does not convert to an integer (that would truncate).
--  expect: invalid conversion|illegal operand for .*conversion
with Interfaces; use Interfaces;
with Zxv.Rationals; use Zxv.Rationals;
procedure Rational_To_Integer is
   R : constant Rational := To_Rational (7, 2);
   I : constant Integer_64 := Integer_64 (R);
begin
   null;
end Rational_To_Integer;
