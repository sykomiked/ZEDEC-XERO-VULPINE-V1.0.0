--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  test_zxv - the Ada binding against sdk/conformance/vectors.json (through
--  the generated package Conformance_Vectors): status names and classes,
--  rationals (operators, status forms, parse, IEEE-754, Q16), four-valued
--  logic, every hexagram table, the quarantine log chain, capabilities,
--  the ledger through *_cap calls, handle lifetimes (R6), and the
--  integer-with-scale decimals.
with Ada.Strings.Fixed;
with Interfaces;
with Conformance_Vectors;
with Test_Support;
with Zxv;
with Zxv.Authority;
with Zxv.Authority.Ledger;
with Zxv.Authority.Quarantine;
with Zxv.Decimals;
with Zxv.Fixed;
with Zxv.Hexagrams;
with Zxv.Logic;
with Zxv.Rationals;

procedure Test_Zxv is

   use Interfaces;
   use Test_Support;
   use type Zxv.Status_Code;
   use type Zxv.Status_Class;
   use type Zxv.Rationals.Rational;
   use type Zxv.Logic.Truth;
   use type Zxv.Hexagrams.Hexagram;
   use type Zxv.Hexagrams.Base;
   use type Zxv.Hexagrams.Polarity;
   use type Zxv.Hexagrams.Phase;
   use type Zxv.Hexagrams.Action;
   use type Zxv.Authority.Rights;
   use type Zxv.Authority.Ledger.Account_Id;
   use type Zxv.Authority.Ledger.Entry_Id;
   use type Zxv.Authority.Ledger.Account_Kind;
   use type Zxv.Authority.Quarantine.Digest;

   package V renames Conformance_Vectors;
   package R renames Zxv.Rationals;
   package ZL renames Zxv.Logic;
   package H renames Zxv.Hexagrams;
   package A renames Zxv.Authority;
   package Led renames Zxv.Authority.Ledger;
   package Q renames Zxv.Authority.Quarantine;

   function Eq (X : R.Rational; N, D : Integer_64) return Boolean is
     (R.Numerator (X) = N and R.Denominator (X) = D);

   function Img (I : Integer_64) return String is
     (Ada.Strings.Fixed.Trim (Integer_64'Image (I), Ada.Strings.Left));

   ----------------------------------------------------------------------
   procedure Test_Library is
      Ver : constant Zxv.ABI_Version := Zxv.Library_Version;
   begin
      Check (Ver.Major = 1 and Ver.Minor >= 3, "ABI 1.3 or a later 1.x");
   end Test_Library;

   procedure Test_Status is
   begin
      for I in V.Status_Code'Range loop
         declare
            C : constant Zxv.Status_Code := Zxv.Status_Code (V.Status_Code (I));
         begin
            Check (Zxv.Status_Name (C) = V.Status_Name (I).all, "status name " & V.Status_Name (I).all);
            Check (Zxv.Status_Class'Pos (Zxv.Class_Of (C)) = Integer (V.Status_Class (I)),
                   "status class " & V.Status_Name (I).all);
         end;
      end loop;
      Check (Zxv.Class_Of (-12345) = Zxv.Class_System, "an unknown code is SYSTEM");
      Check (Zxv.Status_Name (Zxv.ZE_E_Fit) = "ZE_E_FIT" and Zxv.Class_Of (Zxv.ZE_E_Fit) = Zxv.Class_Refused,
             "pillar code ZE_E_FIT");
      Check (Zxv.Class_Of (Zxv.ZE_E_Syntax) = Zxv.Class_Usage, "pillar code ZE_E_SYNTAX is USAGE");
      Check (Zxv.Class_Of (Zxv.E_Poisoned) = Zxv.Class_Fatal and Zxv.Class_Of (Zxv.E_Invariant) = Zxv.Class_Fatal,
             "POISONED and INVARIANT are FATAL");
   end Test_Status;

   ----------------------------------------------------------------------
   procedure Test_Rational_Vectors is
      X  : R.Rational;
      St : Zxv.Status_Code;
      Qv : Integer_32;
   begin
      for I in V.Rat_Cmp'Range loop
         declare
            Ra : constant R.Rational := R.To_Rational (V.Rat_A (I, 0), V.Rat_A (I, 1));
            Rb : constant R.Rational := R.To_Rational (V.Rat_B (I, 0), V.Rat_B (I, 1));
            W  : constant String := R.Image (Ra) & " " & R.Image (Rb);
         begin
            Check (Eq (Ra + Rb, V.Rat_Add (I, 0), V.Rat_Add (I, 1)), "add " & W);
            Check (Eq (Ra - Rb, V.Rat_Sub (I, 0), V.Rat_Sub (I, 1)), "sub " & W);
            Check (Eq (Ra * Rb, V.Rat_Mul (I, 0), V.Rat_Mul (I, 1)), "mul " & W);
            Check (Eq (Ra / Rb, V.Rat_Div (I, 0), V.Rat_Div (I, 1)), "div " & W);
            R.Add (Ra, Rb, X, St);
            Check (St = Zxv.Ok and Eq (X, V.Rat_Add (I, 0), V.Rat_Add (I, 1)), "Add " & W);
            R.Subtract (Ra, Rb, X, St);
            Check (St = Zxv.Ok and Eq (X, V.Rat_Sub (I, 0), V.Rat_Sub (I, 1)), "Subtract " & W);
            R.Multiply (Ra, Rb, X, St);
            Check (St = Zxv.Ok and Eq (X, V.Rat_Mul (I, 0), V.Rat_Mul (I, 1)), "Multiply " & W);
            R.Divide (Ra, Rb, X, St);
            Check (St = Zxv.Ok and Eq (X, V.Rat_Div (I, 0), V.Rat_Div (I, 1)), "Divide " & W);
            Check (R.Compare (Ra, Rb) = Integer (V.Rat_Cmp (I)), "cmp " & W);
            case V.Rat_Cmp (I) is
               when -1 => Check (Ra < Rb and Ra <= Rb and Ra /= Rb and not (Ra >= Rb), "< " & W);
               when 0 => Check (Ra = Rb and Ra <= Rb and Ra >= Rb, "= " & W);
               when others => Check (Ra > Rb and Ra >= Rb and Ra /= Rb and not (Ra <= Rb), "> " & W);
            end case;
         end;
      end loop;

      for I in V.Parse_Text'Range loop
         R.Parse (V.Parse_Text (I).all, X, St);
         Check (St = Zxv.Status_Code (V.Parse_Status (I)), "parse status '" & V.Parse_Text (I).all & "'");
         if St = Zxv.Ok then
            Check (Eq (X, V.Parse_Value (I, 0), V.Parse_Value (I, 1)), "parse value " & V.Parse_Text (I).all);
            Check (R.Image (X) = V.Parse_Format (I).all, "image " & V.Parse_Format (I).all);
         end if;
      end loop;

      for I in V.IEEE_Bits'Range loop
         R.From_IEEE_754_Bits (V.IEEE_Bits (I), X, St);
         Check (St = Zxv.Status_Code (V.IEEE_Status (I)), "ieee754 status" & Unsigned_64'Image (V.IEEE_Bits (I)));
         if St = Zxv.Ok then
            Check (Eq (X, V.IEEE_Value (I, 0), V.IEEE_Value (I, 1)), "ieee754 value");
            Check (R.From_IEEE_754_Bits (V.IEEE_Bits (I)) = X, "ieee754 function form");
         end if;
      end loop;

      for I in V.Q16_Q'Range loop
         R.To_Q16 (R.To_Rational (V.Q16_Value (I, 0), V.Q16_Value (I, 1)), Qv, St);
         Check (St = Zxv.Status_Code (V.Q16_Status (I)) and (St /= Zxv.Ok or Qv = V.Q16_Q (I)),
                "q16 " & Img (V.Q16_Value (I, 0)) & "/" & Img (V.Q16_Value (I, 1)));
      end loop;
   end Test_Rational_Vectors;

   procedure Test_Rational_API is
      Third : constant R.Rational := R.To_Rational (1, 3);
      X     : R.Rational := R.To_Rational (7);
      St    : Zxv.Status_Code;
      Big   : constant R.Rational := R.To_Rational (3_037_000_500);
      procedure Overflow is
         Y : constant R.Rational := R.To_Rational (Integer_64'Last - 1) + 2;
      begin
         Check (R.Is_Zero (Y), "unreachable");
      end Overflow;
      procedure Bad_Parse is
         Y : constant R.Rational := R.Parse ("1e5");
      begin
         Check (R.Is_Zero (Y), "unreachable");
      end Bad_Parse;
      procedure Bad_Bits is
         Y : constant R.Rational := R.From_IEEE_754_Bits (16#7ff8000000000000#);
      begin
         Check (R.Is_Zero (Y), "unreachable");
      end Bad_Bits;
   begin
      Check (Eq (Third + 1, 4, 3) and Eq (1 - Third, 2, 3), "mixed with Int64");
      Check (Eq (Third * 3, 1, 1) and Eq (2 / Third, 6, 1), "mixed * and /");
      Check (Eq (-Third, -1, 3) and Eq (abs (-Third), 1, 3), "negation and abs");
      Check (Eq (Third ** 3, 1, 27) and Eq (Third ** (-2), 9, 1) and Eq (Third ** 0, 1, 1), "**");
      Check (Eq (R.To_Rational (6, -4), -3, 2), "normalized: denominator positive, reduced");
      Check (Eq (R.To_Rational (2) ** 62, 4_611_686_018_427_387_904, 1), "2 ** 62 fits");
      Check (R.Sign (-Third) = -1 and R.Sign (R.Zero) = 0 and R.Sign (Third) = 1, "Sign");
      R.Multiply (Big, Big, X, St);
      Check (St = Zxv.E_Range and R.Is_Zero (X), "3037000500 squared does not fit: ZXV_E_RANGE");
      R.Divide (Third, R.Zero, X, St);
      Check (St = Zxv.E_Arg, "division by zero is ZXV_E_ARG");
      R.Normalize (1, 0, X, St);
      Check (St = Zxv.E_Arg, "denominator 0");
      R.Normalize (Integer_64'First, 1, X, St);
      Check (St /= Zxv.Ok, "INT64_MIN is refused");
      Raises ("overflow raises", Zxv.E_Range, Overflow'Access);
      Raises ("no exponents", Zxv.E_Format, Bad_Parse'Access);
      Raises ("NaN is refused", Zxv.E_Float, Bad_Bits'Access);
      declare
         Items : constant R.Rational_Array :=
           (R.To_Rational (1, 2), R.To_Rational (-3, 4), R.To_Rational (1, 4), R.Zero);
         W     : constant R.Rational_Array := (R.To_Rational (2), R.To_Rational (4, 3), R.To_Rational (4),
                                               R.To_Rational (9));
      begin
         Check (R.Is_Zero (R.Sum (Items)), "Sum");
         Check (Eq (R.Dot (Items, W), 1, 1), "Dot");
      end;
      Check (R.Image (R.To_Rational (-51, 4)) = "-12.75" and R.Image (Third) = "1/3", "Image");
      Check (R.To_Q16 (R.To_Rational (1, 2)) = 32_768, "To_Q16");
      -- R11-BOUNDARY-BEGIN: the one place the tests pass a floating-point literal.
      Check (Eq (R.From_IEEE_754 (0.1), 3_602_879_701_896_397, 36_028_797_018_963_968)
             and R.From_IEEE_754 (0.5) = R.To_Rational (1, 2), "From_IEEE_754 is the exact double");
      -- R11-BOUNDARY-END
   end Test_Rational_API;

   ----------------------------------------------------------------------
   procedure Test_Truth is
      X : ZL.Truth;
   begin
      for Ia in 0 .. 3 loop
         declare
            Ta : constant ZL.Truth := ZL.From_Code (Integer_32 (Ia));
         begin
            Check (ZL.Code (Ta) = Integer_32 (Ia) and ZL.Name (Ta) = V.LP_Name (Ia).all, "truth " & V.LP_Name (Ia).all);
            X := not Ta;
            Check (ZL.Code (X) = V.LP_Not (Ia), "not " & V.LP_Name (Ia).all);
            for Ib in 0 .. 3 loop
               declare
                  Tb : constant ZL.Truth := ZL.From_Code (Integer_32 (Ib));
               begin
                  Check (ZL.Code (Ta and Tb) = V.LP_And (Ia, Ib), "and " & V.LP_Name (Ia).all & " " & V.LP_Name (Ib).all);
                  Check (ZL.Code (Ta or Tb) = V.LP_Or (Ia, Ib), "or " & V.LP_Name (Ia).all & " " & V.LP_Name (Ib).all);
               end;
            end loop;
         end;
      end loop;
      Check (ZL.From_Evidence (2, 0) = ZL.True and ZL.From_Evidence (0, 1) = ZL.False
             and ZL.From_Evidence (1, 1) = ZL.Both and ZL.From_Evidence (0, 0) = ZL.Neither, "From_Evidence");
      Check (not ZL.To_Boolean (ZL.Both, On_Both => False, On_Neither => True), "policy for Both");
      Check (ZL.To_Boolean (ZL.Neither, On_Both => False, On_Neither => True), "policy for Neither");
      Check (ZL.To_Boolean (ZL.True, False, False) and not ZL.To_Boolean (ZL.False, True, True),
             "True and False ignore the policy");
   end Test_Truth;

   ----------------------------------------------------------------------
   procedure Test_Hexagram is
      Hx, Y : H.Hexagram;
      St    : Zxv.Status_Code;
      Bad   : Natural := 0;
   begin
      for T in 0 .. 63 loop
         Hx := H.From_Tag (H.Tag_Value (T));
         Check (Integer (H.Tag (Hx)) = T and H.Name (Hx) = V.Hex_Name (T).all, "hex name " & V.Hex_Name (T).all);
         Check (H.Base'Enum_Rep (H.Base_Of (Hx)) = Integer (V.Hex_Base (T))
                and H.Polarity'Enum_Rep (H.Polarity_Of (Hx)) = Integer (V.Hex_Polarity (T))
                and H.Phase'Enum_Rep (H.Phase_Of (Hx)) = Integer (V.Hex_Phase (T)), "hex fields " & V.Hex_Name (T).all);
         Check (H.Action'Enum_Rep (H.Action_Of (Hx)) = Integer (V.Hex_Action (T))
                and H.Name (H.Action_Of (Hx)) = V.Hex_Action_Name (Integer (V.Hex_Action (T))).all,
                "hex action " & V.Hex_Name (T).all);
         Check (H.Pack (H.Base_Of (Hx), H.Polarity_Of (Hx), H.Phase_Of (Hx)) = Hx, "Pack");
         Check (Integer (H.Tag (not Hx)) = Integer (V.Hex_Not (T)), "hex not " & V.Hex_Name (T).all);
         Y := H.Forbid (Hx);
         Check (H.Polarity_Of (Y) = H.Forbidden and H.Phase_Of (Y) = H.Phase_Of (Hx), "Forbid keeps the phase");
         for U in 0 .. 63 loop
            if Integer (H.Tag (Hx and H.From_Tag (H.Tag_Value (U)))) /= Integer (V.Hex_And (T, U))
              or Integer (H.Tag (Hx or H.From_Tag (H.Tag_Value (U)))) /= Integer (V.Hex_Or (T, U))
            then
               Bad := Bad + 1;
            end if;
         end loop;
         for P in H.Phase loop
            H.Step (Hx, P, Y, St);
            if V.Hex_Step (T, H.Phase'Enum_Rep (P)) = -1 then
               Check (St = Zxv.E_State and Y = Hx, "step refused " & V.Hex_Name (T).all);
            else
               Check (St = Zxv.Ok and Integer (H.Tag (Y)) = Integer (V.Hex_Step (T, H.Phase'Enum_Rep (P))),
                      "step " & V.Hex_Name (T).all);
               Check (H.Step (Hx, P) = Y, "Step function form");
            end if;
         end loop;
         for B in H.Base loop
            for Pl in H.Polarity loop
               H.Set (Hx, B, Pl, Y, St);
               declare
                  Want : constant Integer_32 := V.Hex_Set (T, H.Base'Enum_Rep (B) * 4 + H.Polarity'Enum_Rep (Pl));
               begin
                  if Want = -1 then
                     Check (St = Zxv.E_State and Y = Hx, "set refused");
                  else
                     Check (St = Zxv.Ok and Integer (H.Tag (Y)) = Integer (Want), "set");
                  end if;
               end;
            end loop;
         end loop;
      end loop;
      Check (Bad = 0, "64 x 64 and/or tables (" & Natural'Image (Bad) & " differ)");
      for K in 0 .. 3 loop
         Check (H.Base'Enum_Rep (H.From_Truth (ZL.From_Code (Integer_32 (K)))) = Integer (V.Hex_LP_To_Base (K)),
                "truth -> base");
         Check (ZL.Code (H.To_Truth (H.Base'Enum_Val (K))) = V.Hex_Base_To_LP (K), "base -> truth");
         Check (H.Phase'Enum_Rep (H.Phase_Next (H.Phase'Enum_Val (K))) = Integer (V.Hex_Phase_Next (K)), "Phase_Next");
      end loop;
      for K in V.Pol_Result'Range loop
         Check (H.Polarity'Enum_Rep (H.Polarity_From_Evidence
                  (Unsigned_32 (V.Pol_Pos0 (K)), Unsigned_32 (V.Pol_Neg0 (K)), Unsigned_32 (V.Pol_Pos1 (K)),
                   Unsigned_32 (V.Pol_Neg1 (K)), V.Pol_Stale (K))) = Integer (V.Pol_Result (K)),
                "polarity from evidence" & Integer'Image (K));
      end loop;
      Check (H.Action_Of (H.Pack (H.Hex_True, H.Negentropic, H.Manifest)) = H.Proceed, "true/negentropic proceeds");
      Hx := H.Forbid (H.Pack (H.Hex_True, H.Negentropic, H.Emerging));
      H.Set (Hx, H.Hex_True, H.Negentropic, Y, St);
      Check (St = Zxv.E_State and Y = Hx and H.Action_Of (Hx) = H.Rollback, "a forbidden tag cannot be un-forbidden");
   end Test_Hexagram;

   ----------------------------------------------------------------------
   function To_Digest (T : Integer) return Q.Digest is
      D : Q.Digest;
   begin
      for I in D'Range loop
         D (I) := V.QLog_Subject (T, I - 1);
      end loop;
      return D;
   end To_Digest;

   procedure Test_Quarantine is
      Ctx   : A.Context;
      Root  : A.Capability;
      Plain : A.Capability;
      Lg    : Q.Log;
      Small : Q.Log;
      procedure No_Attest is
         L2 : Q.Log;
      begin
         L2.Create (Plain, 4);
      end No_Attest;
      procedure Full is
         S : constant Unsigned_64 := Small.Append (Root, H.From_Tag (10), 1, 1, Q.Zero_Digest);
      begin
         Check (S = 99, "unreachable");
      end Full;
      procedure Append_No_Attest is
         S : constant Unsigned_64 := Lg.Append (Plain, H.From_Tag (10), 1, 1, Q.Zero_Digest);
      begin
         Check (S = 99, "unreachable");
      end Append_No_Attest;
   begin
      Ctx.Create;
      Root.Create_Root (Ctx, A.All_Rights);
      Plain.Attenuate (Root, (A.Ledger_Read => True, others => False));
      Raises ("a log needs Attest", Zxv.E_Cap, No_Attest'Access);
      Lg.Create (Root, 8);
      Check (Lg.Count = 0 and Lg.Head = Q.Zero_Digest, "empty log");
      for I in V.QLog_Seq'Range loop
         Check (Lg.Append (Root, H.From_Tag (H.Tag_Value (V.QLog_Tag (I))), Unsigned_8 (V.QLog_Reason (I)),
                           Unsigned_32 (V.QLog_Submitter (I)), To_Digest (I)) = Unsigned_64 (V.QLog_Seq (I)), "seq");
      end loop;
      Check (Lg.Count = V.QLog_Seq'Length, "count");
      Check (Q.Hex (Lg.Head) = V.QLog_Head, "the head equals vectors.json");
      for I in V.QLog_Seq'Range loop
         declare
            Rv : constant Q.Record_View := Lg.Record_At (I - 1);
         begin
            Check (Rv.Seq = Unsigned_64 (V.QLog_Seq (I)) and Integer (H.Tag (Rv.Tag)) = Integer (V.QLog_Tag (I))
                   and Integer (Rv.Reason) = Integer (V.QLog_Reason (I))
                   and Integer_64 (Rv.Submitter) = V.QLog_Submitter (I) and Rv.Subject = To_Digest (I),
                   "record fields");
            Check (Q.Hex (Rv.Prev) = V.QLog_Prev (I).all and Q.Hex (Rv.Hash) = V.QLog_Hash (I).all, "record chain");
         end;
      end loop;
      Lg.Verify;
      Check (True, "verify");
      Small.Create (Root, 1);
      Check (Small.Append (Root, H.From_Tag (10), 1, 1, Q.Zero_Digest) = 0, "first append");
      Raises ("a full log is ZXV_E_CAPACITY", Zxv.E_Capacity, Full'Access);
      Raises ("appending needs Attest", Zxv.E_Cap, Append_No_Attest'Access);
   end Test_Quarantine;

   ----------------------------------------------------------------------
   procedure Test_Capabilities_And_Ledger is
      Ctx            : A.Context;
      Root           : A.Capability;
      Poster, Reader : A.Capability;
      Post_Rights    : constant A.Rights := (A.Ledger_Read | A.Ledger_Post => True, others => False);
      Read_Rights    : constant A.Rights := (A.Ledger_Read => True, others => False);
      Bank, Alice, Bob : Led.Account_Id;
      E1, E2         : Led.Entry_Id;
      St             : Zxv.Status_Code;
      procedure Reader_Posts is
         E : constant Led.Entry_Id := Led.Post (Reader, Alice, Bob, 1, "NGN", "R-1");
      begin
         Check (E = 0, "unreachable");
      end Reader_Posts;
      procedure Require_Post is
      begin
         Reader.Require (Post_Rights);
      end Require_Post;
      procedure Revoked_Read is
         B : constant Led.Balance := Led.Balance_Of (Reader, Bank);
      begin
         Check (B.Debit = -1, "unreachable");
      end Revoked_Read;
   begin
      Ctx.Create;
      Check (not Ctx.Is_Poisoned, "a new context is not poisoned");
      Root.Create_Root (Ctx, A.All_Rights, Process_Id => 7, Id => (others => 16#5A#));
      Poster.Attenuate (Root, Post_Rights or A.Rights'(A.Swarm => True, others => False));
      Reader.Attenuate (Poster, Read_Rights);
      Check (Poster.Granted = (Post_Rights or A.Rights'(A.Swarm => True, others => False)) and Reader.Granted = Read_Rights, "attenuation narrows the rights");
      Check (Root.Process_Id = 7 and Root.Attestation = ZL.True, "process id and attestation");
      Check (Reader.Allows (Read_Rights) and not Reader.Allows (Post_Rights), "Allows");
      Raises ("Require refuses", Zxv.E_Cap, Require_Post'Access);

      Led.Enable_Currency (Poster, "NGN");
      Bank := Led.Open_Account (Poster, "bank", "NGN", Led.Issuer);
      Alice := Led.Open_Account (Poster, "alice", "NGN", Led.Holder);
      Bob := Led.Open_Account (Poster, "bob", "NGN", Led.Holder);
      Check (Led.Account_Count (Reader) = 3 and Led.Account_Name (Reader, Alice) = "alice"
             and Led.Account_Currency (Reader, Bob) = "NGN" and Led.Kind_Of (Reader, Bank) = Led.Issuer,
             "accounts");
      E1 := Led.Post (Poster, Bank, Alice, 100_000, "NGN", "FUND-1");
      E2 := Led.Post (Poster, Alice, Bob, 25_025, "NGN", "PAY-1");
      Check (E1 = 1 and E2 = 2, "entry ids");
      Led.Try_Post (Poster, Bob, Alice, 30_000, "NGN", "PAY-2", E1, St);
      Check (St = Zxv.E_Funds, "insufficient funds");
      Led.Try_Post (Poster, Alice, Bob, 1, "NGN", "PAY-1", E1, St);
      Check (St = Zxv.E_Duplicate, "duplicate reference");
      Led.Try_Post (Poster, Alice, Bob, 0, "NGN", "PAY-3", E1, St);
      Check (St = Zxv.E_Range, "amount 0");
      Led.Try_Post (Poster, Alice, Bob, 1, "USD", "PAY-4", E1, St);
      Check (St = Zxv.E_Currency, "USD is not enabled");
      Raises ("a reader cannot post", Zxv.E_Cap, Reader_Posts'Access);
      declare
         Bb : constant Led.Balance := Led.Balance_Of (Reader, Bank);
         Ba : constant Led.Balance := Led.Balance_Of (Reader, Alice);
         Bo : constant Led.Balance := Led.Balance_Of (Reader, Bob);
      begin
         Check (Bb.Equity = -100_000 and Ba.Equity = 74_975 and Bo.Equity = 25_025, "balances");
         Check (Ba.Debit = 100_000 and Ba.Credit = 25_025, "debit and credit");
         Check (Bb.Equity + Ba.Equity + Bo.Equity = 0, "the books balance");
      end;
      Check (Led.Entry_Count (Reader, Alice) = 2, "two journal lines");
      declare
         J : constant Led.Journal_Line := Led.Entry_At (Reader, Alice, 1);
      begin
         Check (J.Amount = -25_025 and J.Counterparty = Bob and J.Reference (1 .. J.Reference_Length) = "PAY-1",
                "journal line");
      end;
      Led.Verify (Reader);
      Check (not Ctx.Is_Poisoned, "not poisoned");

      --  An attestation is the capability's own; a child does not see a later
      --  change on its parent (zxv_api.h).
      Poster.Set_Attestation (ZL.Both);
      Check (Poster.Attestation = ZL.Both, "Attestation");
      Led.Try_Post (Poster, Alice, Bob, 1, "NGN", "PAY-5", E1, St);
      Check (St = Zxv.E_Cap, "a contradictory attestation blocks");
      Poster.Set_Attestation (ZL.True);
      Led.Try_Post (Poster, Alice, Bob, 1, "NGN", "PAY-5", E1, St);
      Check (St = Zxv.Ok, "attested again");

      Root.Revoke;
      Check (Root.Is_Revoked and not Poster.Allows (Post_Rights) and not Reader.Allows (Read_Rights),
             "revoking the root revokes every descendant");
      Raises ("revoked", Zxv.E_Cap, Revoked_Read'Access);
      Reader.Release;
      Check (not Reader.Is_Held, "Release");
   end Test_Capabilities_And_Ledger;

   ----------------------------------------------------------------------
   --  R6: a capability keeps its context alive; the handles are destroyed
   --  children first however Ada finalizes the objects (ASan checks this
   --  under SANITIZE=1).
   procedure Test_Lifetimes is
      Child : A.Capability;
      Lg    : Q.Log;
      Acct  : Led.Account_Id;
   begin
      declare
         Ctx  : A.Context;
         Root : A.Capability;
      begin
         Ctx.Create;
         Root.Create_Root (Ctx, A.All_Rights);
         Child.Attenuate (Root, (A.Ledger_Read | A.Ledger_Post | A.Attest => True, others => False));
         Lg.Create (Child, 4);
      end;   --  Ctx and Root are finalized here
      Check (Child.Is_Held, "the child outlives its context's variable");
      Led.Enable_Currency (Child, "KES");
      Acct := Led.Open_Account (Child, "a", "KES", Led.Holder);
      Check (Acct = 0, "the ledger still works after its context variable was finalized");
      Check (Lg.Append (Child, H.Forbid (H.From_Tag (0)), 1, 1, Q.Zero_Digest) = 0 and then Lg.Count = 1,
             "the log still works");
   end Test_Lifetimes;

   ----------------------------------------------------------------------
   procedure Test_Decimals is
      package Naira is new Zxv.Decimals (Scale => 2, Digits_Count => 15);
      use type Naira.Amount;
      use type Zxv.Fixed.Decimal;
      Fee : constant Naira.Amount := Naira.Value ("12.50");
      D   : Zxv.Fixed.Decimal;
      Ok  : Boolean;
      procedure Too_Precise is
         X : constant Naira.Amount := Naira.Value ("12.505");
      begin
         Check (X = 0, "unreachable");
      end Too_Precise;
      procedure Third is
         X : constant Naira.Amount := Naira.From_Rational (R.To_Rational (1, 3));
      begin
         Check (X = 0, "unreachable");
      end Third;
      procedure Too_Big is
         X : constant Naira.Amount := Naira.Value ("10000000000000.00");
      begin
         Check (X = 0, "unreachable");
      end Too_Big;
   begin
      Check (Fee = 1250 and Naira.Image (Fee) = "12.50", "Value and Image");
      Check (Naira.Image (-Fee) = "-12.50" and Naira.Image (5) = "0.05", "negative and small");
      Check (Naira.To_Rational (Fee) = R.To_Rational (25, 2), "To_Rational");
      Check (Naira.From_Rational (R.To_Rational (1, 4)) = 25, "1/4 is exactly 0.25");
      Check (Naira.Delta_Value = R.To_Rational (1, 100), "Delta_Value");
      Check (Naira.Value ("12.500") = 1250, "extra zero decimals are accepted");
      Raises ("12.505 does not fit scale 2", Zxv.ZE_E_Fit, Too_Precise'Access);
      Raises ("1/3 does not fit scale 2", Zxv.ZE_E_Fit, Third'Access);
      Raises ("16 digits do not fit digits 15", Zxv.ZE_E_Fit, Too_Big'Access);
      Check (Naira.Amount'Last = 999_999_999_999_999, "15 digits");
      Zxv.Fixed.Value ("-0.07", 3, D, Ok);
      Check (Ok and D = (Value => -70, Scale => 3), "Fixed.Value");
      Check (Zxv.Fixed.Image (D) = "-0.070", "Fixed.Image");
      Check (Zxv.Fixed.Compare ((Value => 1, Scale => 0), (Value => 999, Scale => 3)) = 1, "Compare across scales");
      Check (Zxv.Fixed.Rescale ((Value => 1250, Scale => 2), 1) = (Value => 125, Scale => 1), "Rescale down");
      Zxv.Fixed.Value ("1.2.3", 2, D, Ok);
      Check (not Ok, "malformed text");
      Zxv.Fixed.Value ("1234567890123456789", 0, D, Ok);
      Check (not Ok, "19 digits");
   end Test_Decimals;

begin
   Test_Library;
   Test_Status;
   Test_Rational_Vectors;
   Test_Rational_API;
   Test_Truth;
   Test_Hexagram;
   Test_Quarantine;
   Test_Capabilities_And_Ledger;
   Test_Lifetimes;
   Test_Decimals;
   Finish ("test_zxv");
end Test_Zxv;
