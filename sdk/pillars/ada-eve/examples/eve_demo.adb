--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  eve_demo - the ada-eve pillar end to end (make demo, from the pillar
--  directory):
--    1. examples/payments.ads and examples/payments.eve are one layout;
--    2. a payment is filled in by path, exactly, and refused when a value
--       would have to be rounded;
--    3. it is posted to the libzxv ledger through a capability;
--    4. exact rationals, four-valued truth and a hexagram tag.
with Ada.Exceptions;
with Ada.Text_IO; use Ada.Text_IO;
with Interfaces;
with Zxv;
with Zxv.Authority;
with Zxv.Authority.Ledger;
with Zxv.Decimals;
with Zxv.Eve;
with Zxv.Hexagrams;
with Zxv.Logic;
with Zxv.Rationals;

procedure Eve_Demo is
   package A renames Zxv.Authority;
   package Led renames Zxv.Authority.Ledger;
   package E renames Zxv.Eve;
   package H renames Zxv.Hexagrams;
   package R renames Zxv.Rationals;
   package Naira is new Zxv.Decimals (Scale => 2, Digits_Count => 15);
   use type R.Rational;

   Spec, Short : E.Layout;
   Ctx         : A.Context;
   Root, Clerk : A.Capability;
begin
   --  1. one layout, two spellings
   Spec.Load ("examples/payments.ads");
   Short.Load ("examples/payments.eve");
   Put_Line ("payments.ads digest " & E.Hex (Spec.Package_Digest));
   Put_Line ("payments.eve digest " & E.Hex (Short.Package_Digest));
   Put_Line ("same layout: " & Boolean'Image (E.Same (Spec, Short)));
   Put_Line ("canonical Eve:");
   Put (Spec.Canonical);

   --  2. a payment, exactly
   declare
      Pay : constant E.Type_Ref := Spec.Find ("Payment");
      V   : E.Bytes := Spec.New_Value (Pay);
      Fee : constant Naira.Amount := Naira.Value ("12.50");
      Bank, Alice : Led.Account_Id;
      P   : E.Posting;
   begin
      Ctx.Create;
      Root.Create_Root (Ctx, A.All_Rights);
      Clerk.Attenuate (Root, (A.Ledger_Post | A.Ledger_Read => True, others => False));
      Led.Enable_Currency (Root, "NGN");
      Bank := Led.Open_Account (Root, "bank", "NGN", Led.Issuer);
      Alice := Led.Open_Account (Root, "alice", "NGN", Led.Holder);

      Spec.Put_Text (Pay, V, "ref", "INV-2024-0001");
      Spec.Put_Text (Pay, V, "amount", "1234.56");
      Spec.Put_Text (Pay, V, "ccy", "NGN");
      Spec.Put_Integer (Pay, V, "from", Interfaces.Integer_64 (Bank));
      Spec.Put_Integer (Pay, V, "to", Interfaces.Integer_64 (Alice));
      Spec.Put_Rational (Pay, V, "fee_charge", Naira.To_Rational (Fee));
      Spec.Put_Rational (Pay, V, "breakdown(1)", R.To_Rational (1, 4));
      Put_Line ("amount     " & Spec.Get_Text (Pay, V, "amount") & " (units"
                & Long_Long_Integer'Image (Long_Long_Integer (Spec.Get_Decimal (Pay, V, "amount").Value)) & ")");
      Put_Line ("fee_charge " & Spec.Get_Text (Pay, V, "fee_charge"));
      Put_Line ("breakdown1 " & Spec.Get_Text (Pay, V, "breakdown(1)") & " (1/4 exactly)");
      begin
         Spec.Put_Rational (Pay, V, "amount", R.To_Rational (1, 3));
      exception
         when X : Zxv.Refused_Error =>
            Put_Line ("refused: " & Ada.Exceptions.Exception_Message (X));
      end;

      --  3. post it through the clerk's capability
      P := E.Post (Clerk, Short, Short.Find ("payment"),
                   E.Map (Amount => "amount", From => "from", To => "to", Reference => "ref", Currency => "ccy"), V);
      Put_Line ("posted entry" & Led.Entry_Id'Image (P.Entry_Ref) & ":" & Long_Long_Integer'Image
                  (Long_Long_Integer (P.Amount_Minor)) & " minor units of " & P.Currency (1 .. P.Currency_Length));
      Put_Line ("alice holds" & Long_Long_Integer'Image
                  (Long_Long_Integer (Led.Balance_Of (Clerk, Alice).Equity)) & " kobo");
   end;

   --  4. the exact types
   declare
      Third : constant R.Rational := R.To_Rational (1, 3);
      T     : constant Zxv.Logic.Truth := Zxv.Logic.From_Evidence (Positive => 2, Negative => 1);
      Tag   : constant H.Hexagram := H.Pack (H.Hex_True, H.Negentropic, H.Manifest);
   begin
      Put_Line ("1/3 + 1/6 = " & R.Image (Third + R.To_Rational (1, 6)));
      Put_Line ("evidence 2 for, 1 against: " & Zxv.Logic.Name (T));
      Put_Line ("hexagram " & H.Name (Tag) & " -> " & H.Name (H.Action_Of (Tag)));
      Put_Line ("forbidden " & H.Name (H.Forbid (Tag)) & " -> " & H.Name (H.Action_Of (H.Forbid (Tag))));
   end;
end Eve_Demo;
