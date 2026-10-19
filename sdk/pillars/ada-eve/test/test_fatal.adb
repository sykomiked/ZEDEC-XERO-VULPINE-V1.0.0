--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  test_fatal - a poisoned context stops the program, even inside a
--  "when others" handler and even through the status form Try_Post.
--
--  `make fatal` runs this and requires: exit status 70, ZXV_E_POISONED on
--  standard error, and no AFTER-STOP on standard output. A status form
--  hands back recoverable refusals; it cannot hand back a FATAL one,
--  because nothing on that context can succeed any more and a caller that
--  kept going would be computing with books whose invariants are unknown.
--  This is the Ada counterpart of the Fortran pillar's ERROR STOP 70.
--
--  With the argument "eve" the poisoned call is Zxv.Eve.Try_Post (a
--  record posted through libzeve) instead of Ledger.Try_Post.
with Ada.Command_Line;
with Ada.Text_IO;
with Interfaces;
with Zxv;
with Zxv.Authority;
with Zxv.Authority.Ledger;
with Zxv.Eve;

procedure Test_Fatal is
   use Ada.Text_IO;
   use type Zxv.Status_Code;
   package A renames Zxv.Authority;
   package Led renames Zxv.Authority.Ledger;
   Ctx     : A.Context;
   Root    : A.Capability;
   Bank, B : Led.Account_Id;
   Id      : Led.Entry_Id;
   St      : Zxv.Status_Code;
begin
   Ctx.Create;
   Root.Create_Root (Ctx, A.All_Rights);
   Led.Enable_Currency (Root, "NGN");
   Bank := Led.Open_Account (Root, "bank", "NGN", Led.Issuer);
   B := Led.Open_Account (Root, "alice", "NGN", Led.Holder);
   Led.Try_Post (Root, Bank, B, 100, "NGN", "FUND-1", Id, St);
   if St /= Zxv.Ok then
      Put_Line ("setup failed");
      return;
   end if;

   Ctx.Halt ("test_fatal: deliberate halt");
   Put_Line ("BEFORE-STOP");

   begin
      --  a recoverable refusal would come back in St; ZXV_E_POISONED stops
      if Ada.Command_Line.Argument_Count = 1 and then Ada.Command_Line.Argument (1) = "eve" then
         declare
            L : Zxv.Eve.Layout;
         begin
            L.Parse_Eve ("eve p" & ASCII.LF & "acct = 0 .. 9" & ASCII.LF & "money = dec(9, 2)" & ASCII.LF
                         & "pay { ref: str(8); amount: money; from, to: acct }" & ASCII.LF);
            declare
               T : constant Zxv.Eve.Type_Ref := L.Find ("pay");
               V : Zxv.Eve.Bytes := L.New_Value (T);
               P : Zxv.Eve.Posting;
            begin
               L.Put_Text (T, V, "ref", "PAY-1");
               L.Put_Text (T, V, "amount", "0.01");
               L.Put_Integer (T, V, "from", Interfaces.Integer_64 (Bank));
               L.Put_Integer (T, V, "to", Interfaces.Integer_64 (B));
               Zxv.Eve.Try_Post (Root, L, T, Zxv.Eve.Map ("amount", "from", "to", "ref", Fixed_Currency => "NGN"),
                                 V, P, St);
            end;
         end;
      else
         Led.Try_Post (Root, Bank, B, 1, "NGN", "PAY-1", Id, St);
      end if;
      Put_Line ("AFTER-STOP: Try_Post returned " & Zxv.Status_Name (St));
   exception
      when others =>
         Put_Line ("AFTER-STOP: an exception was caught");
   end;
   Put_Line ("AFTER-STOP: the program kept running");
end Test_Fatal;
