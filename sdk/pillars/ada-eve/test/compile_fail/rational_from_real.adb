--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: To_Rational takes integers; 0.1 has no exact binary value and is not accepted.
--  expect: found type universal real
with Zxv.Rationals; use Zxv.Rationals;
procedure Rational_From_Real is
   R : constant Rational := To_Rational (0.1);
begin
   null;
end Rational_From_Real;
