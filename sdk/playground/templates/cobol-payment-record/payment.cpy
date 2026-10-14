      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      * SPDX-License-Identifier: Apache-2.0
      *
      * PAYMENT.CPY - one payment between two ledger accounts, as a
      * COBOL core declares it. payment.sutra is the same record.
       01  PAYMENT-REC.
           05  PAY-REF             PIC X(16).
           05  PAY-AMOUNT          PIC S9(13)V99 COMP-3.
           05  PAY-CCY             PIC X(3).
           05  PAY-FROM            PIC 9(9) COMP.
           05  PAY-TO              PIC 9(9) COMP.
           05  PAY-VALUE-DATE      PIC 9(8).
           05  PAY-FEE             PIC S9(5)V99.
           05  PAY-STATUS          PIC X.
               88  PAY-NEW             VALUE 'N'.
               88  PAY-POSTED          VALUE 'P'.
           05  FILLER              PIC X(4).
