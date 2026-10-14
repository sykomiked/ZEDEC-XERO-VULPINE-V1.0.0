      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      * SPDX-License-Identifier: Apache-2.0
      *
      * STATEMENT.CPY - one month's statement with a fixed table of
      * lines (OCCURS). The same record is statement.sutra.
       01  STATEMENT-REC.
           05  ST-ACCOUNT          PIC 9(10).
           05  ST-PERIOD.
               10  ST-YEAR         PIC 9(4).
               10  ST-MONTH        PIC 99.
           05  ST-TOTALS           COMP-3.
               10  ST-OPENING      PIC S9(11)V99.
               10  ST-CLOSING      PIC S9(11)V99.
           05  ST-LINE-COUNT       PIC 9(4) COMP.
           05  ST-LINE OCCURS 5 TIMES INDEXED BY ST-IX.
               10  ST-LINE-DATE    PIC 9(8).
               10  ST-LINE-AMOUNT  PIC S9(9)V99
                                   SIGN IS TRAILING SEPARATE CHARACTER.
               10  ST-LINE-RATE    PIC SV9(6) SIGN LEADING.
               10  ST-LINE-TEXT    PIC X(20).
           05  FILLER              PIC X(10) VALUE SPACES.
