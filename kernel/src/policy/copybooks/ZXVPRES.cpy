      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
      * Genetics, LLC. SPDX-License-Identifier: Apache-2.0
      *
      * ZXVPRES - what CALL 'ZXV_POLICY' returns, 41 bytes.
      *   ZP-DECISION  'ALLOW', 'DENY ' or 'FAULT'
      *   ZP-ESCALATE  'Y' when a rule came out BOTH (contradiction):
      *                the transfer is held for a person
      *   ZP-VALUE     'T', 'F', 'B' or 'N' (four-valued result)
      *   ZP-DENY(i) / ZP-BOTH(i)  'Y' per rule: 1 window, 2 lock,
      *                3 signatures, 4 payer signed, 5 asset, 6 cap,
      *                7 limit, 8 payer jurisdiction, 9 payee
      *                jurisdiction, 10 operator pack
      *   ZP-FAULT     0, or the fault code; ZP-WHERE the byte offset
      *                of a contract record fault
       01  ZXV-POLICY-RESULT.
           05  ZP-MAGIC             PIC X(4).
           05  ZP-DECISION          PIC X(5).
           05  ZP-ESCALATE          PIC X.
           05  ZP-VALUE             PIC X.
           05  ZP-DENY              PIC X OCCURS 10 TIMES.
           05  ZP-BOTH              PIC X OCCURS 10 TIMES.
           05  ZP-FAULT             PIC S9(4) SIGN LEADING SEPARATE.
           05  ZP-WHERE             PIC 9(5).
