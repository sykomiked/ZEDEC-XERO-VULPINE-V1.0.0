      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
      * Genetics, LLC. SPDX-License-Identifier: Apache-2.0
      *
      * DISCLAIMER: This engine evaluates deterministic mathematical
      * and state constraints (boolean/paraconsistent policy logic).
      * It does not provide legal advice, make legal determinations,
      * or replace statutory judicial processes. Users are solely
      * responsible for ensuring that parameterized policies comply
      * with applicable local and international laws.
      *
      * ZXVCNTR - one contract rule layer (kernel/src/policy/zxp.h),
      * 4419 bytes, all DISPLAY text. Fill it after INITIALIZE
      * ZXV-CONTRACT: unused slots must stay as INITIALIZE leaves
      * them. Lists (laws, locks, caps, limits, allow, deny) must be
      * in ascending order. Party numbers are 1-based.
      *   ZC-DOC       SHA-256 of the prose agreement, lowercase hex
      *   ZC-SIGN      signatures needed, 1..ZC-PARTY-COUNT
      *   ZC-K-KIND    'DUE ' a payment term, 'GATE' ALL/ANY of
      *                earlier clauses (ZC-K-REF, 1-based)
      *   ZC-K-AFTER   0, or the number of an earlier clause
       01  ZXV-CONTRACT.
           05  ZC-MAGIC             PIC X(4).
           05  ZC-NAME              PIC X(30).
           05  ZC-DATE              PIC 9(8).
           05  ZC-DOC               PIC X(64).
           05  ZC-SIGN              PIC 9(2).
           05  ZC-WIN-FROM          PIC 9(8).
           05  ZC-WIN-TO            PIC 9(8).
           05  ZC-LAW-COUNT         PIC 9.
           05  ZC-PARTY-COUNT       PIC 9(2).
           05  ZC-LOCK-COUNT        PIC 9.
           05  ZC-CAP-COUNT         PIC 9.
           05  ZC-LIMIT-COUNT       PIC 9(2).
           05  ZC-ALLOW-COUNT       PIC 9(2).
           05  ZC-DENY-COUNT        PIC 9(2).
           05  ZC-CLAUSE-COUNT      PIC 9(2).
           05  ZC-LAW               PIC X(6) OCCURS 4 TIMES.
           05  ZC-PARTY             OCCURS 15 TIMES.
               10  ZC-P-ID          PIC X(8).
               10  ZC-P-JUR         PIC X(6).
           05  ZC-LOCK              OCCURS 8 TIMES.
               10  ZC-L-FROM        PIC 9(8).
               10  ZC-L-TO          PIC 9(8).
           05  ZC-CAP               OCCURS 8 TIMES.
               10  ZC-C-ASSET       PIC X(3).
               10  ZC-C-AMOUNT      PIC S9(18) SIGN LEADING SEPARATE.
           05  ZC-LIMIT             OCCURS 32 TIMES.
               10  ZC-M-PARTY       PIC 9(2).
               10  ZC-M-ASSET       PIC X(3).
               10  ZC-M-AMOUNT      PIC S9(18) SIGN LEADING SEPARATE.
           05  ZC-ALLOW             PIC X(6) OCCURS 16 TIMES.
           05  ZC-DENY              PIC X(6) OCCURS 16 TIMES.
           05  ZC-CLAUSE            OCCURS 32 TIMES.
               10  ZC-K-KIND        PIC X(4).
               10  ZC-K-ID          PIC X(8).
               10  ZC-K-PAYER       PIC 9(2).
               10  ZC-K-PAYEE       PIC 9(2).
               10  ZC-K-ASSET       PIC X(3).
               10  ZC-K-AMOUNT      PIC S9(18) SIGN LEADING SEPARATE.
               10  ZC-K-DATE        PIC 9(8).
               10  ZC-K-PENALTY     PIC S9(18) SIGN LEADING SEPARATE.
               10  ZC-K-AFTER       PIC 9(2).
               10  ZC-K-GOP         PIC X(3).
               10  ZC-K-NREF        PIC 9.
               10  ZC-K-REF         PIC 9(2) OCCURS 8 TIMES.
