--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Zxv.Logic - the kernel's paraconsistent four-valued logic.
--
--  Truth is its own enumeration, not Boolean, and there is no implicit
--  conversion: "if T then" does not compile, nor does Boolean (T)
--  (test/compile_fail). Both is a held contradiction, not an error, and
--  Neither is "not yet attested"; collapsing either to a Boolean is a
--  decision the caller writes down with To_Boolean.
with Interfaces;

package Zxv.Logic is

   use type Interfaces.Unsigned_32;

   type Truth is (Neither, True, False, Both);
   for Truth use (Neither => 0, True => 1, False => 2, Both => 3);

   --  The kernel's tables (zxv_lp_not / _and / _or).
   function "not" (A : Truth) return Truth;
   function "and" (A, B : Truth) return Truth;
   function "or" (A, B : Truth) return Truth;

   --  Positive and negative evidence: both -> Both, only positive ->
   --  True, only negative -> False, none -> Neither.
   function From_Evidence (Positive, Negative : Interfaces.Unsigned_32) return Truth
     with Post => (if Positive > 0 and Negative > 0 then From_Evidence'Result = Both);

   --  The one way to a Boolean: the caller says what Both and Neither mean.
   function To_Boolean (A : Truth; On_Both : Boolean; On_Neither : Boolean) return Boolean;

   --  "NEITHER", "TRUE", "FALSE", "BOTH" (zxv_lp_name).
   function Name (A : Truth) return String;

   --  The ZXV_LP_* code, for code that talks to the C ABI directly.
   function Code (A : Truth) return Interfaces.Integer_32;
   function From_Code (C : Interfaces.Integer_32) return Truth
     with Pre => C in 0 .. 3;

end Zxv.Logic;
