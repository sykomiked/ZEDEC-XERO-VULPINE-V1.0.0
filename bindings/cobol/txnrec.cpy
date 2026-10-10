      *> Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      *> SPDX-License-Identifier: Apache-2.0
      *> txnrec.cpy — a sample ZEDEC transaction record copybook. The same
      *> field shapes the C copybook parser (kernel/src/legacy/cobol.c) reads.
       01  TXN-RECORD.
           05  TXN-ID          PIC 9(6).
           05  ACCT-NAME       PIC X(20).
           05  AMOUNT          PIC S9(7)V99 COMP-3.
           05  RAIL-CODE       PIC 9(4) COMP.
