--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: a Rational and a real literal do not mix (no float arithmetic sneaks in).
--  expect: expected type universal real|found private type "Rational"
with Zxv.Rationals; use Zxv.Rationals;
procedure Rational_Real_Literal is
   R : Rational := To_Rational (1, 2);
begin
   R := R + 0.5;
end Rational_Real_Literal;
