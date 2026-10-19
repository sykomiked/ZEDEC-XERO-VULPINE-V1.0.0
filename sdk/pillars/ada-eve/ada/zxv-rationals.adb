--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Ada.Unchecked_Conversion;
with Interfaces.C;
with System;

package body Zxv.Rationals is

   use type Interfaces.C.size_t;

   type Rat_Op is access function (AN, AD, BN, BD : Int64; ON, OD : access Int64) return Status_Code
     with Convention => C;

   function C_Add (AN, AD, BN, BD : Int64; ON, OD : access Int64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_add";
   function C_Sub (AN, AD, BN, BD : Int64; ON, OD : access Int64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_sub";
   function C_Mul (AN, AD, BN, BD : Int64; ON, OD : access Int64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_mul";
   function C_Div (AN, AD, BN, BD : Int64; ON, OD : access Int64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_div";
   function C_Cmp (AN, AD, BN, BD : Int64; Out_Cmp : access Interfaces.Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_cmp";
   function C_Normalize (N, D : Int64; ON, OD : access Int64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_normalize";
   function C_Parse (Text : System.Address; ON, OD : access Int64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_parse";
   function C_Format (N, D : Int64; Buf : System.Address; Cap : Interfaces.C.size_t;
                      Len : access Interfaces.C.size_t) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_format";
   function C_From_IEEE (Bits : Interfaces.Unsigned_64; ON, OD : access Int64) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_from_ieee754";
   function C_To_Q16 (N, D : Int64; Q : access Interfaces.Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_rat_to_q16";

   procedure Apply (Op : Rat_Op; L, R : Rational; Result : out Rational; Status : out Status_Code) is
      N, D : aliased Int64 := 0;
   begin
      Status := Op (L.Num, L.Den, R.Num, R.Den, N'Access, D'Access);
      Result := (if Status = Ok then (N, D) else Zero);
   end Apply;

   function Apply (Op : Rat_Op; L, R : Rational; What : String) return Rational is
      Result : Rational;
      Status : Status_Code;
   begin
      Apply (Op, L, R, Result, Status);
      Check (Status, What, (if Status = Ok then "" else Image (L) & " " & What (What'Last - 1) & " " & Image (R)
                            & " does not fit int64/int64 exactly (never rounded)"));
      return Result;
   end Apply;

   procedure Normalize (N, D : Int64; Result : out Rational; Status : out Status_Code) is
      ON, OD : aliased Int64 := 0;
   begin
      Status := C_Normalize (N, D, ON'Access, OD'Access);
      Result := (if Status = Ok then (ON, OD) else Zero);
   end Normalize;

   function To_Rational (N : Int64; D : Int64 := 1) return Rational is
      Result : Rational;
      Status : Status_Code;
   begin
      Normalize (N, D, Result, Status);
      Check (Status, "Zxv.Rationals.To_Rational");
      return Result;
   end To_Rational;

   function Numerator (R : Rational) return Int64 is (R.Num);
   function Denominator (R : Rational) return Int64 is (R.Den);
   function Is_Zero (R : Rational) return Boolean is (R.Num = 0);
   function Sign (R : Rational) return Integer is (if R.Num > 0 then 1 elsif R.Num < 0 then -1 else 0);

   function "+" (L, R : Rational) return Rational is (Apply (C_Add'Access, L, R, "Zxv.Rationals.""+"""));
   function "-" (L, R : Rational) return Rational is (Apply (C_Sub'Access, L, R, "Zxv.Rationals.""-"""));
   function "*" (L, R : Rational) return Rational is (Apply (C_Mul'Access, L, R, "Zxv.Rationals.""*"""));
   function "/" (L, R : Rational) return Rational is (Apply (C_Div'Access, L, R, "Zxv.Rationals.""/"""));
   function "-" (R : Rational) return Rational is (Zero - R);
   function "abs" (R : Rational) return Rational is (if R.Num < 0 then -R else R);

   function "**" (L : Rational; Exponent : Integer) return Rational is
      Base   : constant Rational := (if Exponent < 0 then One / L else L);
      Result : Rational := One;
   begin
      for I in 1 .. abs Exponent loop
         Result := Result * Base;
      end loop;
      return Result;
   end "**";

   function "+" (L : Rational; R : Int64) return Rational is (L + To_Rational (R));
   function "+" (L : Int64; R : Rational) return Rational is (To_Rational (L) + R);
   function "-" (L : Rational; R : Int64) return Rational is (L - To_Rational (R));
   function "-" (L : Int64; R : Rational) return Rational is (To_Rational (L) - R);
   function "*" (L : Rational; R : Int64) return Rational is (L * To_Rational (R));
   function "*" (L : Int64; R : Rational) return Rational is (To_Rational (L) * R);
   function "/" (L : Rational; R : Int64) return Rational is (L / To_Rational (R));
   function "/" (L : Int64; R : Rational) return Rational is (To_Rational (L) / R);

   function Compare (L, R : Rational) return Integer is
      C : aliased Interfaces.Integer_32 := 0;
   begin
      Check (C_Cmp (L.Num, L.Den, R.Num, R.Den, C'Access), "Zxv.Rationals.Compare");
      return Integer (C);
   end Compare;

   function "<" (L, R : Rational) return Boolean is (Compare (L, R) < 0);
   function "<=" (L, R : Rational) return Boolean is (Compare (L, R) <= 0);
   function ">" (L, R : Rational) return Boolean is (Compare (L, R) > 0);
   function ">=" (L, R : Rational) return Boolean is (Compare (L, R) >= 0);

   procedure Add (L, R : Rational; Result : out Rational; Status : out Status_Code) is
   begin
      Apply (C_Add'Access, L, R, Result, Status);
   end Add;

   procedure Subtract (L, R : Rational; Result : out Rational; Status : out Status_Code) is
   begin
      Apply (C_Sub'Access, L, R, Result, Status);
   end Subtract;

   procedure Multiply (L, R : Rational; Result : out Rational; Status : out Status_Code) is
   begin
      Apply (C_Mul'Access, L, R, Result, Status);
   end Multiply;

   procedure Divide (L, R : Rational; Result : out Rational; Status : out Status_Code) is
   begin
      Apply (C_Div'Access, L, R, Result, Status);
   end Divide;

   procedure Parse (Text : String; Result : out Rational; Status : out Status_Code) is
      T    : constant String := C_Text (Text);
      N, D : aliased Int64 := 0;
   begin
      Status := C_Parse (T'Address, N'Access, D'Access);
      Result := (if Status = Ok then (N, D) else Zero);
   end Parse;

   function Parse (Text : String) return Rational is
      Result : Rational;
      Status : Status_Code;
   begin
      Parse (Text, Result, Status);
      Check (Status, "Zxv.Rationals.Parse", (if Status = Ok then "" else "'" & Text & "' is not an exact number"));
      return Result;
   end Parse;

   function Image (R : Rational) return String is
      Buf : String (1 .. 96);
      Len : aliased Interfaces.C.size_t := 0;
   begin
      Check (C_Format (R.Num, R.Den, Buf'Address, Buf'Length, Len'Access), "Zxv.Rationals.Image");
      return Buf (1 .. Natural (Len));
   end Image;

   procedure To_Q16 (R : Rational; Result : out Interfaces.Integer_32; Status : out Status_Code) is
      Q : aliased Interfaces.Integer_32 := 0;
   begin
      Status := C_To_Q16 (R.Num, R.Den, Q'Access);
      Result := (if Status = Ok then Q else 0);
   end To_Q16;

   function To_Q16 (R : Rational) return Interfaces.Integer_32 is
      Q : Interfaces.Integer_32;
      S : Status_Code;
   begin
      To_Q16 (R, Q, S);
      Check (S, "Zxv.Rationals.To_Q16");
      return Q;
   end To_Q16;

   function Sum (Items : Rational_Array) return Rational is
      Result : Rational := Zero;
   begin
      for X of Items loop
         Result := Result + X;
      end loop;
      return Result;
   end Sum;

   function Dot (A, B : Rational_Array) return Rational is
      Result : Rational := Zero;
   begin
      for I in A'Range loop
         Result := Result + A (I) * B (B'First + (I - A'First));
      end loop;
      return Result;
   end Dot;

   procedure From_IEEE_754_Bits (Bits : Interfaces.Unsigned_64; Result : out Rational;
                                 Status : out Status_Code) is
      N, D : aliased Int64 := 0;
   begin
      Status := C_From_IEEE (Bits, N'Access, D'Access);
      Result := (if Status = Ok then (N, D) else Zero);
   end From_IEEE_754_Bits;

   function From_IEEE_754_Bits (Bits : Interfaces.Unsigned_64) return Rational is
      Result : Rational;
      Status : Status_Code;
   begin
      From_IEEE_754_Bits (Bits, Result, Status);
      Check (Status, "Zxv.Rationals.From_IEEE_754");
      return Result;
   end From_IEEE_754_Bits;

   -- R11-BOUNDARY-BEGIN: the only place this binding touches a float. The
   --  bits of the Long_Float go to zxv_rat_from_ieee754 unchanged; no
   --  floating-point arithmetic happens here.
   pragma Compile_Time_Error (Long_Float'Size /= 64, "Long_Float must be IEEE-754 binary64");
   function Bits_Of is new Ada.Unchecked_Conversion (Long_Float, Interfaces.Unsigned_64);
   function From_IEEE_754 (X : Long_Float) return Rational is (From_IEEE_754_Bits (Bits_Of (X)));
   -- R11-BOUNDARY-END

end Zxv.Rationals;
