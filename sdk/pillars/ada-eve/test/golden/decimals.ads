--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  decimals.ads - the ends of what a decimal type can be: one digit, 18
--  digits, scale 0, scale 18, and a range that is all decimals.
package Decimals is
   type Units is delta 1.0 digits 1;
   type Atto is delta 1.0E-18 digits 18;
   type Big is delta 1.0 digits 18;
   type Rate is delta 0.000_1 digits 5 range -1.0 .. 1.0;
   type Cents is delta 0.01 digits 18;
   type Mix is record
      U : Units;
      A : Atto;
      B : Big;
      R : Rate;
      C : Cents;
   end record;
end Decimals;
