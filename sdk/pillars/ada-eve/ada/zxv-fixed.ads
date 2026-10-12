--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Fixed - integer-with-scale decimals: Value / 10**Scale, with Value
--  an Integer_64 of at most 18 digits. This is how the ada-eve pillar
--  holds a "delta 0.01 digits 15" component: as the integer 123456 with
--  scale 2, never as a binary fraction.
--
--  Written in the SPARK subset (SPARK_Mode). `make prove` runs gnatprove
--  on it when gnatprove is installed; the README says what was proved and
--  with which version. No other package in the binding is SPARK.
with Interfaces;

package Zxv.Fixed with SPARK_Mode is

   use type Interfaces.Integer_64;
   subtype Int64 is Interfaces.Integer_64;

   Max_Scale : constant := 18;
   subtype Scale_Range is Natural range 0 .. Max_Scale;
   subtype Digits_Range is Positive range 1 .. 18;

   Max_Units : constant Int64 := 999_999_999_999_999_999;
   subtype Units is Int64 range -Max_Units .. Max_Units;

   type Pow10_Table is array (Scale_Range) of Int64;
   Pow10 : constant Pow10_Table :=
     (1, 10, 100, 1_000, 10_000, 100_000, 1_000_000, 10_000_000, 100_000_000, 1_000_000_000,
      10_000_000_000, 100_000_000_000, 1_000_000_000_000, 10_000_000_000_000,
      100_000_000_000_000, 1_000_000_000_000_000, 10_000_000_000_000_000,
      100_000_000_000_000_000, 1_000_000_000_000_000_000);

   type Decimal is record
      Value : Units := 0;
      Scale : Scale_Range := 0;
   end record;

   --  D can be written with To decimals exactly and within 18 digits.
   function Fits_Scale (D : Decimal; To : Scale_Range) return Boolean is
     (if To >= D.Scale then abs D.Value <= Max_Units / Pow10 (To - D.Scale)
      else D.Value rem Pow10 (D.Scale - To) = 0);

   function Rescale (D : Decimal; To : Scale_Range) return Decimal
     with Pre  => Fits_Scale (D, To),
          Post => Rescale'Result.Scale = To
                  and then (if To >= D.Scale then Rescale'Result.Value = D.Value * Pow10 (To - D.Scale)
                            else Rescale'Result.Value = D.Value / Pow10 (D.Scale - To));

   function Add (A, B : Decimal) return Decimal
     with Pre  => A.Scale = B.Scale and then A.Value + B.Value in Units,
          Post => Add'Result.Scale = A.Scale and Add'Result.Value = A.Value + B.Value;

   function Subtract (A, B : Decimal) return Decimal
     with Pre  => A.Scale = B.Scale and then A.Value - B.Value in Units,
          Post => Subtract'Result.Scale = A.Scale and Subtract'Result.Value = A.Value - B.Value;

   --  -1, 0 or 1; the scales may differ (compared without overflow).
   function Compare (A, B : Decimal) return Integer
     with Post => Compare'Result in -1 .. 1;

   --  "-1234.56": a sign only when negative, all Scale decimals, at least
   --  one digit before the point.
   Image_Max : constant := 21;
   function Image (D : Decimal) return String
     with Post => Image'Result'First = 1 and Image'Result'Length in 1 .. Image_Max;

   --  Parse "[-]digits[.digits]" exactly into Scale decimals. Ok is False
   --  for malformed text, more than 18 digits, or decimals beyond Scale
   --  that are not zero (never rounded).
   procedure Value (Text : String; Scale : Scale_Range; Result : out Decimal; Ok : out Boolean)
     with Post => (if Ok then Result.Scale = Scale);

end Zxv.Fixed;
