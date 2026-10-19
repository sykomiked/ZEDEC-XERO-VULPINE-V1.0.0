--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  must NOT compile: two decimal instances are different types: kobo do not add to cents.
--  expect: invalid operand types for operator "\+"
with Zxv.Decimals;
procedure Decimals_Mix is
   package Naira is new Zxv.Decimals (Scale => 2, Digits_Count => 15);
   package Yen is new Zxv.Decimals (Scale => 0, Digits_Count => 15);
   use type Naira.Amount;
   A : constant Naira.Amount := 100;
   B : constant Yen.Amount := 100;
   C : constant Naira.Amount := A + B;
begin
   null;
end Decimals_Mix;
