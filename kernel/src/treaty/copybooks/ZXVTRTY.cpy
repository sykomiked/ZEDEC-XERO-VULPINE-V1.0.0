      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
      * Genetics, LLC. SPDX-License-Identifier: Apache-2.0
      *
      * ZXVTRTY - one ZXV treaty, the record CALL 'ZXV_ENGINE' reads
      * (kernel/src/treaty/zxt.h). 2542 bytes, all DISPLAY text.
      * Fill it after INITIALIZE ZXV-TREATY: the engine refuses a
      * record whose unused slots differ from what INITIALIZE leaves.
      *   ZT-MAGIC     'ZXT1'
      *   ZT-NAME      A-Z 0-9 -, starts with a letter, left-justified
      *   ZT-ASSET     three capital letters
      *   ZT-OP        'NET ' nets only; 'SETL' nets, posts the legs
      *                with the assurance fee and pays claims out
      *   ZT-P-WNUM / ZT-P-WDEN  weight in lowest terms; the weights
      *                of all parties must add up to exactly 1
      *   ZT-P-CLAIM   may not exceed ZT-P-DEPOSIT
      *   ZT-O-FROM / ZT-O-TO    party numbers, 1-based
       01  ZXV-TREATY.
           05  ZT-MAGIC             PIC X(4).
           05  ZT-NAME              PIC X(30).
           05  ZT-ASSET             PIC X(3).
           05  ZT-OP                PIC X(4).
           05  ZT-PARTY-COUNT       PIC 9(2).
           05  ZT-OWE-COUNT         PIC 9(3).
           05  ZT-PARTY             OCCURS 16 TIMES.
               10  ZT-P-ID          PIC X(8).
               10  ZT-P-DEPOSIT     PIC S9(18) SIGN LEADING SEPARATE.
               10  ZT-P-CLAIM       PIC S9(18) SIGN LEADING SEPARATE.
               10  ZT-P-WNUM        PIC 9(9).
               10  ZT-P-WDEN        PIC 9(9).
           05  ZT-OWE               OCCURS 64 TIMES.
               10  ZT-O-FROM        PIC 9(2).
               10  ZT-O-TO          PIC 9(2).
               10  ZT-O-AMOUNT      PIC S9(18) SIGN LEADING SEPARATE.
