--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Interfaces.C.Strings;

package body Zxv.Logic is

   function C_Not (A : Interfaces.Integer_32; Out_V : access Interfaces.Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_lp_not";
   function C_And (A, B : Interfaces.Integer_32; Out_V : access Interfaces.Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_lp_and";
   function C_Or (A, B : Interfaces.Integer_32; Out_V : access Interfaces.Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_lp_or";
   function C_Evidence (P, N : Interfaces.Unsigned_32; Out_V : access Interfaces.Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_lp_from_evidence";
   function C_Name (A : Interfaces.Integer_32) return Interfaces.C.Strings.chars_ptr
     with Import, Convention => C, External_Name => "zxv_lp_name";

   function Code (A : Truth) return Interfaces.Integer_32 is (Truth'Enum_Rep (A));
   function From_Code (C : Interfaces.Integer_32) return Truth is (Truth'Enum_Val (C));

   function Result (S : Status_Code; V : Interfaces.Integer_32; What : String) return Truth is
   begin
      Check (S, What);
      if V not in 0 .. 3 then
         Check (E_Internal, What, "the kernel returned a value outside the four truth values");
      end if;
      return From_Code (V);
   end Result;

   function "not" (A : Truth) return Truth is
      V : aliased Interfaces.Integer_32 := 0;
      S : constant Status_Code := C_Not (Code (A), V'Access);
   begin
      return Result (S, V, "Zxv.Logic.""not""");
   end "not";

   function "and" (A, B : Truth) return Truth is
      V : aliased Interfaces.Integer_32 := 0;
      S : constant Status_Code := C_And (Code (A), Code (B), V'Access);
   begin
      return Result (S, V, "Zxv.Logic.""and""");
   end "and";

   function "or" (A, B : Truth) return Truth is
      V : aliased Interfaces.Integer_32 := 0;
      S : constant Status_Code := C_Or (Code (A), Code (B), V'Access);
   begin
      return Result (S, V, "Zxv.Logic.""or""");
   end "or";

   function From_Evidence (Positive, Negative : Interfaces.Unsigned_32) return Truth is
      V : aliased Interfaces.Integer_32 := 0;
      S : constant Status_Code := C_Evidence (Positive, Negative, V'Access);
   begin
      return Result (S, V, "Zxv.Logic.From_Evidence");
   end From_Evidence;

   function To_Boolean (A : Truth; On_Both : Boolean; On_Neither : Boolean) return Boolean is
   begin
      case A is
         when True    => return Standard.True;
         when False   => return Standard.False;
         when Both    => return On_Both;
         when Neither => return On_Neither;
      end case;
   end To_Boolean;

   function Name (A : Truth) return String is (Interfaces.C.Strings.Value (C_Name (Code (A))));

end Zxv.Logic;
