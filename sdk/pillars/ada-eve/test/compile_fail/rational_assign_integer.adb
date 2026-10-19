--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Rational is private: an integer literal is not one (use To_Rational).
--  expect: expected private type .*Rational
with Zxv.Rationals; use Zxv.Rationals;
procedure Rational_Assign_Integer is
   R : Rational := 1;
begin
   null;
end Rational_Assign_Integer;
