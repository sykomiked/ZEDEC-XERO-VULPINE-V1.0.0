--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  limits.ads - the int64 range and every encoding width at its edges.
package Limits is
   type Wide is range -2 ** 63 .. 2 ** 63 - 1;
   type U8 is range 0 .. 255;
   type S8 is range -128 .. 127;
   type U16 is range 0 .. 2 ** 16 - 1;
   type S16 is range -2 ** 15 .. 2 ** 15 - 1;
   type U32 is range 0 .. 2 ** 32 - 1;
   type S32 is range -2 ** 31 .. 2 ** 31 - 1;
   type U33 is range 0 .. 2 ** 32;
   type Single is range 7 .. 7;
   type Edges is record
      W                 : Wide;
      A                 : U8;
      B                 : S8;
      C                 : U16;
      D                 : S16;
      E                 : U32;
      F                 : S32;
      G                 : U33;
      H                 : Single;
   end record;
end Limits;
