--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  test_eve_ada - Zxv.Eve: the Ada and Eve forms of examples/payments are
--  one layout (same digest), the canonical text round-trips, values are
--  read and written exactly by path, refusals raise with the right status
--  and position, and a record is posted to the ledger through a
--  capability. Run from the pillar directory (it reads examples/).
with Ada.Characters.Latin_1;
with Interfaces;
with Test_Support;
with Zxv;
with Zxv.Authority;
with Zxv.Authority.Ledger;
with Zxv.Eve;
with Zxv.Fixed;
with Zxv.Rationals;

procedure Test_Eve_Ada is

   use Interfaces;
   use Test_Support;
   use type Zxv.Status_Code;
   use type Zxv.Eve.Type_Ref;
   use type Zxv.Eve.Kind;
   use type Zxv.Eve.Bytes;
   use type Zxv.Eve.Digest;
   use type Zxv.Fixed.Decimal;
   use type Zxv.Rationals.Rational;
   use type Zxv.Authority.Ledger.Entry_Id;

   package E renames Zxv.Eve;
   package A renames Zxv.Authority;
   package Led renames Zxv.Authority.Ledger;

   LF : constant Character := Ada.Characters.Latin_1.LF;

   Payments_Digest : constant String := "2c3a553116257c1d64a240a9d44ce63bbc61ccabd2a03b0595006af5ba945b46";

   Ada_Form, Eve_Form : E.Layout;

   procedure Test_Same is
      Again, Printed : E.Layout;
      Other          : E.Layout;
   begin
      Ada_Form.Load ("examples/payments.ads");
      Eve_Form.Load ("examples/payments.eve");
      Check (Ada_Form.Name = "payments" and Eve_Form.Name = "payments", "Name");
      Check (E.Same (Ada_Form, Eve_Form), "the Ada and Eve forms are one layout");
      Check (E.Hex (Ada_Form.Package_Digest) = Payments_Digest, "digest " & E.Hex (Ada_Form.Package_Digest));
      Check (Ada_Form.Canonical = Eve_Form.Canonical, "one canonical text");
      Again.Parse_Eve (Ada_Form.Canonical);
      Check (Again.Package_Digest = Ada_Form.Package_Digest, "canonical Eve re-parses to the same digest");
      Printed.Parse_Ada (Eve_Form.As_Ada);
      Check (E.Same (Printed, Ada_Form), "printed Ada re-parses to the same layout");
      Other.Parse_Eve ("eve payments" & LF & "money = dec(15, 3)" & LF);
      Check (not E.Same (Other, Ada_Form), "a different layout differs");
      Other.Unload;
      Check (not Other.Is_Loaded, "Unload");
      Other.Parse_Ada ("package Payments is type Money is delta 0.01 digits 15; end Payments;");
      Check (Other.Type_Count = 2, "Boolean and Money");
   end Test_Same;

   procedure Test_Types is
      Pay : constant E.Type_Ref := Ada_Form.Find ("Payment");
      Ti  : constant E.Type_Info := Ada_Form.Info (Pay);
      Mi  : constant E.Type_Info := Ada_Form.Info (Ada_Form.Find ("money"));
      Fi  : constant E.Type_Info := Ada_Form.Info (Ada_Form.Find ("FEE"));
      Li  : constant E.Type_Ref := Ada_Form.Find ("lines");
      procedure Missing is
         T : constant E.Type_Ref := Ada_Form.Find ("nothing");
      begin
         Check (T = 0, "unreachable");
      end Missing;
   begin
      Check (Ti.Of_Kind = E.Record_Kind and Ti.Size = 74 and Ti.Count = 9, "payment is a record of 74 bytes");
      Check (Ti.Name (1 .. Ti.Name_Length) = "payment", "lower-case name");
      Check (Mi.Of_Kind = E.Decimal_Kind and Mi.Digits_Count = 15 and Mi.Scale = 2
             and Mi.Hi = 999_999_999_999_999 and Mi.Size = 8, "money");
      Check (Fi.Is_Subtype and Fi.Base = Ada_Form.Find ("money") and Fi.Lo = -99_999, "fee is a subtype of money");
      Check (Ada_Form.Info (Li).Of_Kind = E.Array_Kind and Ada_Form.Dimension (Li, 1).Index = Ada_Form.Find ("line_no")
             and Ada_Form.Dimension (Li, 1).Lo = 1 and Ada_Form.Dimension (Li, 1).Hi = 4, "lines (line_no)");
      declare
         C : constant E.Component_Info := Ada_Form.Component (Pay, 4);
      begin
         Check (C.Name (1 .. C.Name_Length) = "from" and C.Offset = 25, "component 4 is from at 25");
      end;
      Check (Ada_Form.Literal (Ada_Form.Find ("currency"), 4) = "usd", "literal 4");
      Check (Ada_Form.Literal (E.Boolean_Type, 1) = "true", "Boolean is type 0");
      Raises ("an unknown type", Zxv.E_Not_Found, Missing'Access);
   end Test_Types;

   procedure Test_Values is
      L   : E.Layout renames Ada_Form;
      Pay : constant E.Type_Ref := L.Find ("payment");
      V   : E.Bytes := L.New_Value (Pay);
      W   : E.Bytes (1 .. V'Length);
      procedure Inexact is
      begin
         L.Put_Rational (Pay, V, "amount", Zxv.Rationals.To_Rational (1, 3));
      end Inexact;
      procedure Fee_Too_Big is
      begin
         L.Put_Text (Pay, V, "fee_charge", "1000.00");
      end Fee_Too_Big;
      procedure Bad_State is
      begin
         L.Put_Text (Pay, V, "state", "refused");
      end Bad_State;
      procedure Long_Ref is
      begin
         L.Put_Text (Pay, V, "ref", "THIS-IS-17-BYTES!");
      end Long_Ref;
      procedure Bad_Index is
      begin
         L.Put_Text (Pay, V, "breakdown(5)", "1");
      end Bad_Index;
      procedure Bad_Path is
      begin
         L.Put_Text (Pay, V, "breakdown(", "1");
      end Bad_Path;
      procedure No_Such is
      begin
         L.Put_Text (Pay, V, "amount_due", "1");
      end No_Such;
      procedure Bad_Check is
      begin
         L.Check_Value (Pay, W);
      end Bad_Check;
   begin
      Check (V'Length = 74, "New_Value is 74 bytes");
      L.Check_Value (Pay, V);
      Check (L.Is_Valid (Pay, V), "the initial value is valid");
      Check (L.Get_Integer (Pay, V, "value_date") = 1900_01_01, "nearest zero inside the range");
      Check (L.Get_Text (Pay, V, "ref") = (1 .. 16 => ' '), "String starts as spaces");
      L.Put_Text (Pay, V, "ref", "INV-2024-0001");
      L.Put_Text (Pay, V, "amount", "1234.56");
      L.Put_Text (Pay, V, "ccy", "NGN");
      L.Put_Integer (Pay, V, "from", 0);
      L.Put_Integer (Pay, V, "to", 1);
      L.Put_Decimal (Pay, V, "fee_charge", (Value => -25, Scale => 1));
      L.Put_Rational (Pay, V, "breakdown(2)", Zxv.Rationals.To_Rational (1, 4));
      L.Put_Position (Pay, V, "state", 1);
      Check (L.Get_Decimal (Pay, V, "amount") = (Value => 123_456, Scale => 2), "amount is 123456 at scale 2");
      Check (L.Get_Rational (Pay, V, "amount") = Zxv.Rationals.To_Rational (123_456, 100), "amount as a rational");
      Check (L.Get_Text (Pay, V, "fee_charge") = "-2.50", "fee_charge -2.50");
      Check (L.Get_Text (Pay, V, "breakdown(2)") = "0.25", "1/4 is 0.25 exactly");
      Check (L.Get_Text (Pay, V, "ccy") = "ngn" and L.Get_Position (Pay, V, "CCY") = 0, "ccy");
      Check (L.Get_Text (Pay, V, "state") = "posted", "state");
      Check (L.Get_Text (Pay, V, "ref") = "INV-2024-0001   ", "ref padded with spaces");
      --  the canonical encoding: amount is big-endian 8 bytes at offset 16
      Check (V (17 .. 24) = (0, 0, 0, 0, 0, 16#01#, 16#E2#, 16#40#), "amount bytes big-endian");
      W := V;
      Raises ("1/3 is not a cent amount", Zxv.ZE_E_Fit, Inexact'Access);
      Raises ("fee outside its subtype", Zxv.ZE_E_Fit, Fee_Too_Big'Access);
      Raises ("state outside fresh .. posted", Zxv.ZE_E_Fit, Bad_State'Access);
      Raises ("17 bytes into String (1 .. 16)", Zxv.ZE_E_Fit, Long_Ref'Access);
      Raises ("index 5 of line_no", Zxv.E_Range, Bad_Index'Access);
      Raises ("a malformed path", Zxv.E_Format, Bad_Path'Access);
      Raises ("an unknown component", Zxv.E_Not_Found, No_Such'Access);
      Check (V = W, "a refused put leaves the value unchanged");
      --  state is byte 74: 2 (refused) is outside fresh .. posted
      W (74) := 2;
      Check (not L.Is_Valid (Pay, W), "Is_Valid sees a bad byte");
      Raises ("Check_Value names the bad scalar", Zxv.ZE_E_Data, Bad_Check'Access);
      --  the same bytes read through the Eve form
      Check (Eve_Form.Get_Text (Eve_Form.Find ("payment"), V, "amount") = "1234.56", "Eve form reads the same bytes");
   end Test_Values;

   procedure Test_Refusals is
      L      : E.Layout;
      St     : Zxv.Status_Code;
      Where  : E.Diagnostic;
      procedure Float_Type is
         X : E.Layout;
      begin
         X.Parse_Ada ("package P is" & LF & "   type F is digits 6;" & LF & "end P;");
      end Float_Type;
   begin
      L.Try_Parse ("package P is" & LF & "   type T is mod 256;" & LF & "end P;", E.Ada_Spec, St, Where);
      Check (St = Zxv.ZE_E_Unsupported and Where.Line = 2 and not L.Is_Loaded, "mod is UNSUPPORTED on line 2");
      Check (Where.Message (1 .. 8) = "line 2, ", "the message starts with the position");
      L.Try_Parse ("eve p" & LF & "x = 5 .. 1" & LF, E.Eve_Text, St, Where);
      Check (St = Zxv.ZE_E_Constraint and Where.Line = 2, "an empty range is CONSTRAINT");
      L.Try_Parse ("eve p" & LF & "x = 1 .. 2" & LF & "X = 1 .. 3" & LF, E.Eve_Text, St, Where);
      Check (St = Zxv.ZE_E_Name and Where.Line = 3 and Where.Column = 1, "a duplicate name at 3:1");
      L.Try_Parse ("eve p" & LF & "x = {" & LF, E.Eve_Text, St, Where);
      Check (St = Zxv.ZE_E_Syntax, "syntax");
      Raises ("digits is a float type", Zxv.ZE_E_Unsupported, Float_Type'Access);
      L.Try_Parse ("eve p" & LF & "x = 1 .. 2" & LF, E.Eve_Text, St, Where);
      Check (St = Zxv.Ok and L.Is_Loaded, "then a good parse");
   end Test_Refusals;

   procedure Test_Post is
      Ctx           : A.Context;
      Root, Reader  : A.Capability;
      L             : E.Layout renames Eve_Form;
      Pay           : constant E.Type_Ref := L.Find ("payment");
      V             : E.Bytes := L.New_Value (Pay);
      M             : constant E.Posting_Map :=
        E.Map (Amount => "amount", From => "from", To => "to", Reference => "ref", Currency => "ccy");
      P             : E.Posting;
      St            : Zxv.Status_Code;
      Bank, Alice   : Led.Account_Id;
      procedure Reader_Posts is
         X : constant E.Posting := E.Post (Reader, L, Pay, M, V);
      begin
         Check (X.Entry_Ref = 0, "unreachable");
      end Reader_Posts;
      procedure Fraction is
         X : constant E.Posting := L.Read_Posting (Pay, E.Map ("amount", "from", "to", "ref",
                                                                Fixed_Currency => "NGN", Minor_Units => 0), V);
      begin
         Check (X.Amount_Minor = 0, "unreachable");
      end Fraction;
   begin
      Ctx.Create;
      Root.Create_Root (Ctx, A.All_Rights);
      Reader.Attenuate (Root, (A.Ledger_Read => True, others => False));
      Led.Enable_Currency (Root, "NGN");
      Bank := Led.Open_Account (Root, "bank", "NGN", Led.Issuer);
      Alice := Led.Open_Account (Root, "alice", "NGN", Led.Holder);
      L.Put_Text (Pay, V, "ref", "INV-1");
      L.Put_Text (Pay, V, "amount", "1234.56");
      L.Put_Text (Pay, V, "ccy", "ngn");
      L.Put_Integer (Pay, V, "from", Integer_64 (Bank));
      L.Put_Integer (Pay, V, "to", Integer_64 (Alice));
      P := L.Read_Posting (Pay, M, V);
      Check (P.Amount_Minor = 123_456 and P.Currency (1 .. P.Currency_Length) = "NGN"
             and P.Reference (1 .. P.Reference_Length) = "INV-1" and P.Entry_Ref = 0, "Read_Posting");
      Raises ("1234.56 in a unit with no minor digits", Zxv.ZE_E_Fit, Fraction'Access);
      Raises ("a reader cannot post", Zxv.E_Cap, Reader_Posts'Access);
      P := E.Post (Root, L, Pay, M, V);
      Check (P.Entry_Ref = 1, "posted as entry 1");
      Check (Led.Balance_Of (Reader, Alice).Equity = 123_456, "alice holds 1234.56 NGN");
      E.Try_Post (Root, L, Pay, M, V, P, St);
      Check (St = Zxv.E_Duplicate, "the same reference twice is ZXV_E_DUPLICATE");
      L.Put_Text (Pay, V, "ref", "INV-2");
      L.Put_Integer (Pay, V, "from", Integer_64 (Alice));
      L.Put_Integer (Pay, V, "to", Integer_64 (Bank));
      L.Put_Text (Pay, V, "amount", "5000.00");
      E.Try_Post (Root, L, Pay, M, V, P, St);
      Check (St = Zxv.E_Funds, "a holder cannot go below zero");
   end Test_Post;

begin
   Test_Same;
   Test_Types;
   Test_Values;
   Test_Refusals;
   Test_Post;
   Finish ("test_eve_ada");
end Test_Eve_Ada;
