--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Hexagrams - the 64-state hexagram tags of ABI 1.3 (zxv_hex_*).
--
--  A Hexagram is base / polarity / phase, two bits each. The base codes
--  are the line patterns, NOT the Truth codes (Hex_False is 0, Truth's
--  False is 2): convert with From_Truth and To_Truth only. A Hexagram is
--  not a Truth and not an integer; neither converts implicitly
--  (test/compile_fail). Every rule is the kernel's table:
--    a forbidden polarity always means Rollback, and it is absorbing;
--    the phase moves one step at a time on the Gray cycle.
with Interfaces;
with Zxv.Logic;

package Zxv.Hexagrams is

   use type Interfaces.Unsigned_8;

   type Base is (Hex_False, Hex_Neither, Hex_Both, Hex_True);
   for Base use (Hex_False => 0, Hex_Neither => 1, Hex_Both => 2, Hex_True => 3);

   type Polarity is (Entropic, Neutral, Forbidden, Negentropic);
   for Polarity use (Entropic => 0, Neutral => 1, Forbidden => 2, Negentropic => 3);

   type Phase is (Potential, Emerging, Dissolving, Manifest);
   for Phase use (Potential => 0, Emerging => 1, Dissolving => 2, Manifest => 3);

   type Action is (Proceed, Wait, Refuse, Escalate, Rollback);
   for Action use (Proceed => 0, Wait => 1, Refuse => 2, Escalate => 3, Rollback => 4);

   subtype Tag_Value is Interfaces.Unsigned_8 range 0 .. 63;

   type Hexagram is private;

   function Pack (B : Base; P : Polarity; F : Phase) return Hexagram
     with Post => Base_Of (Pack'Result) = B and Polarity_Of (Pack'Result) = P
                  and Phase_Of (Pack'Result) = F;
   function From_Tag (T : Tag_Value) return Hexagram
     with Post => Tag (From_Tag'Result) = T;
   function Tag (H : Hexagram) return Tag_Value;

   function Base_Of (H : Hexagram) return Base;
   function Polarity_Of (H : Hexagram) return Polarity;
   function Phase_Of (H : Hexagram) return Phase;

   function Action_Of (H : Hexagram) return Action
     with Post => (if Polarity_Of (H) = Forbidden then Action_Of'Result = Rollback);

   function Phase_Next (F : Phase) return Phase;

   --  Move to the same or the next phase. Anything else is a contract
   --  violation here; the status form below reports ZXV_E_STATE instead.
   function Step (H : Hexagram; To : Phase) return Hexagram
     with Pre  => To = Phase_Of (H) or else To = Phase_Next (Phase_Of (H)),
          Post => Phase_Of (Step'Result) = To and Base_Of (Step'Result) = Base_Of (H)
                  and Polarity_Of (Step'Result) = Polarity_Of (H);
   procedure Step (H : Hexagram; To : Phase; Result : out Hexagram; Status : out Status_Code);

   --  New base and polarity, same phase. Forbidden stays forbidden.
   function Set (H : Hexagram; B : Base; P : Polarity) return Hexagram
     with Pre  => Polarity_Of (H) /= Forbidden or else P = Forbidden,
          Post => Phase_Of (Set'Result) = Phase_Of (H);
   procedure Set (H : Hexagram; B : Base; P : Polarity; Result : out Hexagram; Status : out Status_Code);

   function Forbid (H : Hexagram) return Hexagram
     with Post => Polarity_Of (Forbid'Result) = Forbidden and Base_Of (Forbid'Result) = Base_Of (H)
                  and Phase_Of (Forbid'Result) = Phase_Of (H);

   function "and" (A, B : Hexagram) return Hexagram;
   function "or" (A, B : Hexagram) return Hexagram;
   function "not" (A : Hexagram) return Hexagram
     with Post => Polarity_Of ("not"'Result) = Polarity_Of (A) and Phase_Of ("not"'Result) = Phase_Of (A);

   --  Polarity from evidence counts before and after (never Forbidden).
   function Polarity_From_Evidence
     (Pos_Before, Neg_Before, Pos_After, Neg_After : Interfaces.Unsigned_32; Stale : Boolean) return Polarity
     with Post => Polarity_From_Evidence'Result /= Forbidden;

   --  The four-valued bridge (zxv_hex_from_lp / zxv_hex_to_lp).
   function From_Truth (T : Zxv.Logic.Truth) return Base;
   function To_Truth (B : Base) return Zxv.Logic.Truth;

   --  "true/negentropic/manifest"; "rollback".
   function Name (H : Hexagram) return String;
   function Name (A : Action) return String;

private

   type Hexagram is record
      T : Tag_Value := 0;
   end record;

end Zxv.Hexagrams;
