--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Decimals - an implied-scale decimal type for each scale you need:
--
--     package Naira is new Zxv.Decimals (Scale => 2, Digits_Count => 15);
--     Fee : Naira.Amount := Naira.Value ("12.50");   --  the integer 1250
--
--  Amount is a range-constrained integer type, so Ada's own checks keep
--  it inside 15 digits, and two instances are different types: kobo do
--  not add to cents without a conversion you write. The only ways in from
--  text or rationals are exact; 12.505 into scale 2 raises Refused_Error
--  (ZE_E_FIT), never rounds. Built on Zxv.Fixed (SPARK).
with Zxv.Fixed;
with Zxv.Rationals;

generic
   Scale        : Zxv.Fixed.Scale_Range;
   Digits_Count : Zxv.Fixed.Digits_Range;
package Zxv.Decimals is

   use type Zxv.Fixed.Int64;

   Max : constant Zxv.Fixed.Int64 := Zxv.Fixed.Pow10 (Digits_Count) - 1;

   type Amount is new Zxv.Fixed.Int64 range -Max .. Max;

   --  1/10**Scale: the value of Amount'(1).
   function Delta_Value return Zxv.Rationals.Rational;

   function To_Decimal (A : Amount) return Zxv.Fixed.Decimal
     with Post => To_Decimal'Result.Scale = Scale;
   function To_Rational (A : Amount) return Zxv.Rationals.Rational;

   --  Exact, or Refused_Error with ZE_E_FIT.
   function From_Rational (R : Zxv.Rationals.Rational) return Amount;
   function Value (Text : String) return Amount;

   --  "-1234.56", always Scale decimals.
   function Image (A : Amount) return String;

end Zxv.Decimals;
