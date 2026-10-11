      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
      * Genetics, LLC. SPDX-License-Identifier: Apache-2.0
      *
      * ZXVRSLT - what CALL 'ZXV_ENGINE' writes back
      * (kernel/src/treaty/zxt_exec.h). 2422 bytes, all DISPLAY text.
      *   ZR-STATUS    0 = done; negative = refused (RETURN-CODE too)
      *   ZR-WHERE     byte offset of the first fault in ZXV-TREATY
      *   ZR-FEES      assurance fee charged on the posted legs
      *   ZR-NET       owed to the party minus owed by it (gross)
      *   ZR-BALANCE   held by the party after the batch and claim
      *   ZR-DIVIDEND  weight share of the fee's V-Bill dividend part,
      *                reported only (it stays in the dividend pool)
      *   ZR-LEG       the netted legs, party numbers 1-based
       01  ZXV-RESULT.
           05  ZR-MAGIC             PIC X(4).
           05  ZR-STATUS            PIC S9(4) SIGN LEADING SEPARATE.
           05  ZR-WHERE             PIC 9(5).
           05  ZR-PARTY-COUNT       PIC 9(2).
           05  ZR-LEG-COUNT         PIC 9(3).
           05  ZR-FEES              PIC S9(18) SIGN LEADING SEPARATE.
           05  ZR-PARTY             OCCURS 16 TIMES.
               10  ZR-NET           PIC S9(18) SIGN LEADING SEPARATE.
               10  ZR-BALANCE       PIC S9(18) SIGN LEADING SEPARATE.
               10  ZR-DIVIDEND      PIC S9(18) SIGN LEADING SEPARATE.
           05  ZR-LEG               OCCURS 64 TIMES.
               10  ZR-L-FROM        PIC 9(2).
               10  ZR-L-TO          PIC 9(2).
               10  ZR-L-AMOUNT      PIC S9(18) SIGN LEADING SEPARATE.
