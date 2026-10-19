--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
package body Zxv.Decimals is

   use Zxv.Rationals;

   function Delta_Value return Rational is (To_Rational (1, Zxv.Fixed.Pow10 (Scale)));

   function To_Decimal (A : Amount) return Zxv.Fixed.Decimal is ((Value => Zxv.Fixed.Int64 (A), Scale => Scale));

   function To_Rational (A : Amount) return Rational is
     (To_Rational (Zxv.Fixed.Int64 (A), Zxv.Fixed.Pow10 (Scale)));

   function From_Rational (R : Rational) return Amount is
      Units  : Rational;
      Status : Status_Code;
   begin
      Multiply (R, To_Rational (Zxv.Fixed.Pow10 (Scale)), Units, Status);
      if Status /= Ok or else Denominator (Units) /= 1 or else Numerator (Units) not in -Max .. Max then
         Check (ZE_E_Fit, "Zxv.Decimals.From_Rational",
                Image (R) & " has no exact value with" & Natural'Image (Scale) & " decimals and"
                & Natural'Image (Digits_Count) & " digits (never rounded)");
      end if;
      return Amount (Numerator (Units));
   end From_Rational;

   function Value (Text : String) return Amount is
      D  : Zxv.Fixed.Decimal;
      Ok : Boolean;
   begin
      Zxv.Fixed.Value (Text, Scale, D, Ok);
      if not Ok or else D.Value not in -Max .. Max then
         Check (ZE_E_Fit, "Zxv.Decimals.Value",
                "'" & Text & "' is not an exact value with" & Natural'Image (Scale) & " decimals and"
                & Natural'Image (Digits_Count) & " digits (never rounded)");
      end if;
      return Amount (D.Value);
   end Value;

   function Image (A : Amount) return String is (Zxv.Fixed.Image (To_Decimal (A)));

end Zxv.Decimals;
