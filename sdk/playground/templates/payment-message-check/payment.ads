--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  payment.ads - the same payment record as an Ada package specification.
--  An Ada system and the Eve checker agree on the layout exactly when
--  this file and payment.eve have the same digest.
package Payments is
   pragma Pure;

   type Currency is
     (USD, EUR, GBP, JPY, CHF, CAD, AUD, CNY, INR, BRL, NGN, GHS, KES, ZAR);
   type Account_Id is range 0 .. 2**32 - 1;
   type Money is delta 0.01 digits 15;
   subtype Amount_Due is Money range 0.01 .. 9_999_999_999_999.99;
   type Date8 is range 1900_01_01 .. 2999_12_31;   --  YYYYMMDD

   type Payment is record
      Ref              : String (1 .. 16);
      Amount           : Amount_Due;
      Ccy              : Currency;
      Debtor, Creditor : Account_Id;
      Value_Date       : Date8;
   end record;
end Payments;
