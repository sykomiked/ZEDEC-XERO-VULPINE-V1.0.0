--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  payments.ads - a payment record as an Ada package specification.
--  examples/payments.eve declares the same types in Eve; both have the
--  same digest (zeve same examples/payments.ads examples/payments.eve).
package Payments is
   pragma Pure;

   type Currency is (NGN, GHS, KES, ZAR, USD);
   type Account_Id is range 0 .. 2**32 - 1;
   type Money is delta 0.01 digits 15;
   subtype Fee is Money range -999.99 .. 999.99;
   type Date8 is range 1900_01_01 .. 2999_12_31;   --  YYYYMMDD
   type Line_No is range 1 .. 4;
   type Status is (Fresh, Posted, Refused);
   type Lines is array (Line_No) of Money;

   type Payment is record
      Ref        : String (1 .. 16);
      Amount     : Money;
      Ccy        : Currency;
      From, To   : Account_Id;
      Value_Date : Date8 := 2024_01_01;
      Fee_Charge : Fee;
      Breakdown  : Lines;
      State      : Status range Fresh .. Posted;
   end record;
end Payments;
