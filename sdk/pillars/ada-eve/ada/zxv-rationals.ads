--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Rationals - exact rationals over libzxv (zxv_rat_*).
--
--  A Rational is a reduced Num/Den pair of Integer_64 with Den > 0 (the
--  type invariant). Every operation is exact; a result that does not fit
--  int64/int64 raises Usage_Error with ZXV_E_RANGE, never rounds.
--
--  There is no conversion from or to a floating-point type except
--  From_IEEE_754, which takes a Long_Float and returns the exact value of
--  that double (0.1 gives 3602879701896397/36028797018963968). Mixing a
--  Rational with a real literal does not compile: R + 0.5 has no "+"
--  (test/compile_fail).
with Interfaces;

package Zxv.Rationals is

   use type Interfaces.Integer_64;
   subtype Int64 is Interfaces.Integer_64;

   type Rational is private
     with Type_Invariant => Is_Reduced (Rational);

   function Is_Reduced (R : Rational) return Boolean;

   Zero : constant Rational;
   One  : constant Rational;

   --  N/D reduced. INT64_MIN is refused by libzxv (its negation is not an
   --  int64), so it is outside the precondition too.
   function To_Rational (N : Int64; D : Int64 := 1) return Rational
     with Pre  => D /= 0 and N /= Int64'First and D /= Int64'First,
          Post => Denominator (To_Rational'Result) > 0;

   function Numerator (R : Rational) return Int64;
   function Denominator (R : Rational) return Int64
     with Post => Denominator'Result > 0;

   function Is_Zero (R : Rational) return Boolean;
   function Sign (R : Rational) return Integer
     with Post => Sign'Result in -1 .. 1;

   function "+" (L, R : Rational) return Rational;
   function "-" (L, R : Rational) return Rational;
   function "*" (L, R : Rational) return Rational;
   function "/" (L, R : Rational) return Rational
     with Pre => not Is_Zero (R);
   function "-" (R : Rational) return Rational;
   function "abs" (R : Rational) return Rational
     with Post => Sign ("abs"'Result) >= 0;
   function "**" (L : Rational; Exponent : Integer) return Rational
     with Pre => not (Is_Zero (L) and Exponent < 0);

   --  With an integer on either side (R + 1, 2 * R).
   function "+" (L : Rational; R : Int64) return Rational;
   function "+" (L : Int64; R : Rational) return Rational;
   function "-" (L : Rational; R : Int64) return Rational;
   function "-" (L : Int64; R : Rational) return Rational;
   function "*" (L : Rational; R : Int64) return Rational;
   function "*" (L : Int64; R : Rational) return Rational;
   function "/" (L : Rational; R : Int64) return Rational
     with Pre => R /= 0;
   function "/" (L : Int64; R : Rational) return Rational
     with Pre => not Is_Zero (R);

   --  Exact comparison (zxv_rat_cmp); "=" is the predefined equality of
   --  reduced pairs, which is exact equality.
   function Compare (L, R : Rational) return Integer
     with Post => Compare'Result in -1 .. 1;
   function "<" (L, R : Rational) return Boolean;
   function "<=" (L, R : Rational) return Boolean;
   function ">" (L, R : Rational) return Boolean;
   function ">=" (L, R : Rational) return Boolean;

   --  Status forms: Status is Ok (and Result is set) or the refusal.
   procedure Add (L, R : Rational; Result : out Rational; Status : out Status_Code);
   procedure Subtract (L, R : Rational; Result : out Rational; Status : out Status_Code);
   procedure Multiply (L, R : Rational; Result : out Rational; Status : out Status_Code);
   procedure Divide (L, R : Rational; Result : out Rational; Status : out Status_Code);
   procedure Normalize (N, D : Int64; Result : out Rational; Status : out Status_Code);

   --  "12.75", "-0.1", "3/4", "42" exactly; anything else raises
   --  Usage_Error (ZXV_E_FORMAT), too large is ZXV_E_RANGE.
   function Parse (Text : String) return Rational;
   procedure Parse (Text : String; Result : out Rational; Status : out Status_Code);
   --  "12.75" when the decimal terminates, else "1/3". Never rounds.
   function Image (R : Rational) return String;

   --  Q16.16 fixed point, rounded half away from zero (zxv_rat_to_q16).
   function To_Q16 (R : Rational) return Interfaces.Integer_32;
   procedure To_Q16 (R : Rational; Result : out Interfaces.Integer_32; Status : out Status_Code);

   type Rational_Array is array (Positive range <>) of Rational;
   function Sum (Items : Rational_Array) return Rational;
   function Dot (A, B : Rational_Array) return Rational
     with Pre => A'Length = B'Length;

   --  The exact value of an IEEE-754 binary64 given as its bits. NaN and
   --  infinities are ZXV_E_FLOAT, a value needing more than 63 bits is
   --  ZXV_E_RANGE (both Usage_Error).
   function From_IEEE_754_Bits (Bits : Interfaces.Unsigned_64) return Rational;
   procedure From_IEEE_754_Bits (Bits : Interfaces.Unsigned_64; Result : out Rational;
                                 Status : out Status_Code);

   -- R11-BOUNDARY-BEGIN: the one floating-point type in the binding.
   --  The exact value of a Long_Float (it must be an IEEE-754 binary64,
   --  checked at compile time). Never called implicitly.
   function From_IEEE_754 (X : Long_Float) return Rational;
   -- R11-BOUNDARY-END

private

   type Rational is record
      Num : Int64 := 0;
      Den : Int64 := 1;
   end record;

   function Is_Reduced (R : Rational) return Boolean is
     (R.Den > 0 and R.Num /= Int64'First and R.Den /= Int64'First);

   Zero : constant Rational := (0, 1);
   One  : constant Rational := (1, 1);

end Zxv.Rationals;
