      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
      * Genetics, LLC. SPDX-License-Identifier: Apache-2.0
      *
      * ZXVPTXN - one proposed transfer for CALL 'ZXV_POLICY'
      * (kernel/src/policy/policy.h), 129 bytes, all DISPLAY text.
      *   ZQ-FROM / ZQ-TO  party numbers in the contract, 1-based
      *   ZQ-SIGNED(i)     'Y' if party i's signature over this
      *                    transfer verified, else 'N' (all 15)
      *   ZQ-DOC           the prose agreement's SHA-256, lowercase
      *                    hex; must equal the contract's ZC-DOC
       01  ZXV-TRANSFER.
           05  ZQ-MAGIC             PIC X(4).
           05  ZQ-FROM              PIC 9(2).
           05  ZQ-TO                PIC 9(2).
           05  ZQ-ASSET             PIC X(3).
           05  ZQ-AMOUNT            PIC S9(18) SIGN LEADING SEPARATE.
           05  ZQ-DATE              PIC 9(8).
           05  ZQ-FROM-JUR          PIC X(6).
           05  ZQ-TO-JUR            PIC X(6).
           05  ZQ-SIGNED            PIC X OCCURS 15 TIMES.
           05  ZQ-DOC               PIC X(64).
