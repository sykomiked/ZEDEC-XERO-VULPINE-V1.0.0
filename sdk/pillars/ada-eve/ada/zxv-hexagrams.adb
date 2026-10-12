--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
with Interfaces.C.Strings;

package body Zxv.Hexagrams is

   use Interfaces;

   function C_Pack (B, P, F : Unsigned_8; T : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_pack";
   function C_Unpack (T : Unsigned_8; B, P, F : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_unpack";
   function C_Action (T : Unsigned_8; A : access Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_action";
   function C_Phase_Next (F : Unsigned_8; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_phase_next";
   function C_Step (T, F : Unsigned_8; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_step";
   function C_Set (T, B, P : Unsigned_8; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_set";
   function C_Forbid (T : Unsigned_8; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_forbid";
   function C_And (A, B : Unsigned_8; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_and";
   function C_Or (A, B : Unsigned_8; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_or";
   function C_Not (A : Unsigned_8; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_not";
   function C_Polarity (P0, N0, P1, N1 : Unsigned_32; Stale : Integer_32; O : access Unsigned_8)
     return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_polarity";
   function C_From_LP (LP : Integer_32; O : access Unsigned_8) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_from_lp";
   function C_To_LP (B : Unsigned_8; O : access Integer_32) return Status_Code
     with Import, Convention => C, External_Name => "zxv_hex_to_lp";
   function C_Name (T : Unsigned_8) return Interfaces.C.Strings.chars_ptr
     with Import, Convention => C, External_Name => "zxv_hex_name";
   function C_Action_Name (A : Integer_32) return Interfaces.C.Strings.chars_ptr
     with Import, Convention => C, External_Name => "zxv_hex_action_name";

   --  A tag from the kernel, checked: it must be 0 .. 63.
   function Tagged_Result (S : Status_Code; T : Unsigned_8; What : String) return Hexagram is
   begin
      Check (S, What);
      if T > 63 then
         Check (E_Internal, What, "the kernel returned a tag above 63");
      end if;
      return (T => T);
   end Tagged_Result;

   function Pack (B : Base; P : Polarity; F : Phase) return Hexagram is
      T : aliased Unsigned_8 := 0;
      S : constant Status_Code :=
        C_Pack (Base'Enum_Rep (B), Polarity'Enum_Rep (P), Phase'Enum_Rep (F), T'Access);
   begin
      return Tagged_Result (S, T, "Zxv.Hexagrams.Pack");
   end Pack;

   function From_Tag (T : Tag_Value) return Hexagram is ((T => T));
   function Tag (H : Hexagram) return Tag_Value is (H.T);

   procedure Fields (H : Hexagram; B, P, F : out Unsigned_8) is
      VB, VP, VF : aliased Unsigned_8 := 0;
   begin
      Check (C_Unpack (H.T, VB'Access, VP'Access, VF'Access), "Zxv.Hexagrams.Unpack");
      B := VB;
      P := VP;
      F := VF;
   end Fields;

   function Base_Of (H : Hexagram) return Base is
      B, P, F : Unsigned_8;
   begin
      Fields (H, B, P, F);
      return Base'Enum_Val (B);
   end Base_Of;

   function Polarity_Of (H : Hexagram) return Polarity is
      B, P, F : Unsigned_8;
   begin
      Fields (H, B, P, F);
      return Polarity'Enum_Val (P);
   end Polarity_Of;

   function Phase_Of (H : Hexagram) return Phase is
      B, P, F : Unsigned_8;
   begin
      Fields (H, B, P, F);
      return Phase'Enum_Val (F);
   end Phase_Of;

   function Action_Of (H : Hexagram) return Action is
      A : aliased Integer_32 := 0;
   begin
      Check (C_Action (H.T, A'Access), "Zxv.Hexagrams.Action_Of");
      return Action'Enum_Val (A);
   end Action_Of;

   function Phase_Next (F : Phase) return Phase is
      O : aliased Unsigned_8 := 0;
   begin
      Check (C_Phase_Next (Phase'Enum_Rep (F), O'Access), "Zxv.Hexagrams.Phase_Next");
      return Phase'Enum_Val (O);
   end Phase_Next;

   procedure Step (H : Hexagram; To : Phase; Result : out Hexagram; Status : out Status_Code) is
      O : aliased Unsigned_8 := 0;
   begin
      Status := C_Step (H.T, Phase'Enum_Rep (To), O'Access);
      Result := (if Status = Ok and O <= 63 then (T => O) else H);
   end Step;

   function Step (H : Hexagram; To : Phase) return Hexagram is
      O : aliased Unsigned_8 := 0;
      S : constant Status_Code := C_Step (H.T, Phase'Enum_Rep (To), O'Access);
   begin
      return Tagged_Result (S, O, "Zxv.Hexagrams.Step");
   end Step;

   procedure Set (H : Hexagram; B : Base; P : Polarity; Result : out Hexagram; Status : out Status_Code) is
      O : aliased Unsigned_8 := 0;
   begin
      Status := C_Set (H.T, Base'Enum_Rep (B), Polarity'Enum_Rep (P), O'Access);
      Result := (if Status = Ok and O <= 63 then (T => O) else H);
   end Set;

   function Set (H : Hexagram; B : Base; P : Polarity) return Hexagram is
      O : aliased Unsigned_8 := 0;
      S : constant Status_Code := C_Set (H.T, Base'Enum_Rep (B), Polarity'Enum_Rep (P), O'Access);
   begin
      return Tagged_Result (S, O, "Zxv.Hexagrams.Set");
   end Set;

   function Forbid (H : Hexagram) return Hexagram is
      O : aliased Unsigned_8 := 0;
      S : constant Status_Code := C_Forbid (H.T, O'Access);
   begin
      return Tagged_Result (S, O, "Zxv.Hexagrams.Forbid");
   end Forbid;

   function "and" (A, B : Hexagram) return Hexagram is
      O : aliased Unsigned_8 := 0;
      S : constant Status_Code := C_And (A.T, B.T, O'Access);
   begin
      return Tagged_Result (S, O, "Zxv.Hexagrams.""and""");
   end "and";

   function "or" (A, B : Hexagram) return Hexagram is
      O : aliased Unsigned_8 := 0;
      S : constant Status_Code := C_Or (A.T, B.T, O'Access);
   begin
      return Tagged_Result (S, O, "Zxv.Hexagrams.""or""");
   end "or";

   function "not" (A : Hexagram) return Hexagram is
      O : aliased Unsigned_8 := 0;
      S : constant Status_Code := C_Not (A.T, O'Access);
   begin
      return Tagged_Result (S, O, "Zxv.Hexagrams.""not""");
   end "not";

   function Polarity_From_Evidence
     (Pos_Before, Neg_Before, Pos_After, Neg_After : Unsigned_32; Stale : Boolean) return Polarity
   is
      O : aliased Unsigned_8 := 0;
   begin
      Check (C_Polarity (Pos_Before, Neg_Before, Pos_After, Neg_After, (if Stale then 1 else 0), O'Access),
             "Zxv.Hexagrams.Polarity_From_Evidence");
      return Polarity'Enum_Val (O);
   end Polarity_From_Evidence;

   function From_Truth (T : Zxv.Logic.Truth) return Base is
      O : aliased Unsigned_8 := 0;
   begin
      Check (C_From_LP (Zxv.Logic.Code (T), O'Access), "Zxv.Hexagrams.From_Truth");
      return Base'Enum_Val (O);
   end From_Truth;

   function To_Truth (B : Base) return Zxv.Logic.Truth is
      O : aliased Integer_32 := 0;
   begin
      Check (C_To_LP (Base'Enum_Rep (B), O'Access), "Zxv.Hexagrams.To_Truth");
      return Zxv.Logic.From_Code (O);
   end To_Truth;

   function Name (H : Hexagram) return String is (Interfaces.C.Strings.Value (C_Name (H.T)));
   function Name (A : Action) return String is
     (Interfaces.C.Strings.Value (C_Action_Name (Action'Enum_Rep (A))));

end Zxv.Hexagrams;
