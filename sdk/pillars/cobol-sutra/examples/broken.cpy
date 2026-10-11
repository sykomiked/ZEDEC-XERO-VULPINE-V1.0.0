      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      * SPDX-License-Identifier: Apache-2.0
      *
      * BROKEN.CPY - refused on purpose: REDEFINES gives one byte two
      * meanings, and libzsutra does not guess which one is meant.
       01  BROKEN-REC.
           05  BR-AMOUNT           PIC S9(7)V99 COMP-3.
           05  BR-AMOUNT-X REDEFINES BR-AMOUNT PIC X(5).
