--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: the IEEE boundary takes a Long_Float (binary64) only; a Float would be widened silently.
--  expect: expected type "(Standard\.)?Long_Float"
with Zxv.Rationals; use Zxv.Rationals;
procedure IEEE_Single is
   X : constant Float := 0.1;
   R : constant Rational := From_IEEE_754 (X);
begin
   null;
end IEEE_Single;
