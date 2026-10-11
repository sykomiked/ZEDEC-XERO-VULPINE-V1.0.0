--  Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  payments_spelled.ads - examples/payments.ads spelled differently:
--  upper case, based and grouped literals, one component per line, no
--  pragma, no default. The layout (and so the digest) is the same.
PACKAGE PAYMENTS IS
   TYPE CURRENCY IS (Ngn, Ghs, Kes, Zar, Usd);
   TYPE ACCOUNT_ID IS RANGE 0 .. 16#FFFF_FFFF#;
   TYPE MONEY IS DELTA 1.0E-2 DIGITS 15;
   SUBTYPE FEE IS MONEY RANGE -999.99 .. 999.99;
   TYPE DATE8 IS RANGE 19000101 .. 29991231;
   TYPE LINE_NO IS RANGE 1 .. 2 * 2;
   TYPE STATUS IS (FRESH, POSTED, REFUSED);
   TYPE LINES IS ARRAY (LINE_NO) OF MONEY;
   TYPE PAYMENT IS RECORD
      REF : STRING (1 .. 16);
      AMOUNT : MONEY;
      CCY : CURRENCY;
      FROM : ACCOUNT_ID;
      TO : ACCOUNT_ID;
      VALUE_DATE : DATE8;
      FEE_CHARGE : FEE;
      BREAKDOWN : LINES;
      STATE : STATUS RANGE FRESH .. POSTED;
   END RECORD;
END PAYMENTS;
