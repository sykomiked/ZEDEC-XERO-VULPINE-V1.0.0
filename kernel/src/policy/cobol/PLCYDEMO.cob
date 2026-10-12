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
      * PLCYDEMO - a COBOL program that fills a contract rule layer
      * from ZXVCNTR, proposes transfers from ZXVPTXN and checks each
      * with CALL 'ZXV_POLICY'. It prints every record so
      * policy_parity.c can compare them byte for byte with the same
      * contract written in Sutra and checked from C.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. PLCYDEMO.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY ZXVCNTR.
       COPY ZXVPTXN.
       COPY ZXVPRES.
       01  W-RC         PIC S9(9).
       PROCEDURE DIVISION.
           DISPLAY "LEN " FUNCTION LENGTH(ZXV-CONTRACT) " "
               FUNCTION LENGTH(ZXV-TRANSFER) " "
               FUNCTION LENGTH(ZXV-POLICY-RESULT)
           INITIALIZE ZXV-CONTRACT
           MOVE 'ZXC1'        TO ZC-MAGIC
           MOVE 'SUPPLY-2027' TO ZC-NAME
           MOVE 20261015      TO ZC-DATE
           MOVE '9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c1
      -         '5b0f00a08'   TO ZC-DOC
           MOVE 2             TO ZC-SIGN
           MOVE 20261015      TO ZC-WIN-FROM
           MOVE 20271231      TO ZC-WIN-TO
           MOVE 1 TO ZC-LAW-COUNT
           MOVE 2 TO ZC-PARTY-COUNT
           MOVE 1 TO ZC-LOCK-COUNT
           MOVE 1 TO ZC-CAP-COUNT
           MOVE 1 TO ZC-LIMIT-COUNT
           MOVE 2 TO ZC-ALLOW-COUNT
           MOVE 1 TO ZC-DENY-COUNT
           MOVE 3 TO ZC-CLAUSE-COUNT
           MOVE 'KE'          TO ZC-LAW(1)
           MOVE 'ACME'        TO ZC-P-ID(1)
           MOVE 'US-DE'       TO ZC-P-JUR(1)
           MOVE 'BETA'        TO ZC-P-ID(2)
           MOVE 'KE'          TO ZC-P-JUR(2)
           MOVE 20261224      TO ZC-L-FROM(1)
           MOVE 20261226      TO ZC-L-TO(1)
           MOVE 'USD'         TO ZC-C-ASSET(1)
           MOVE 2000000       TO ZC-C-AMOUNT(1)
           MOVE 1             TO ZC-M-PARTY(1)
           MOVE 'USD'         TO ZC-M-ASSET(1)
           MOVE 1500000       TO ZC-M-AMOUNT(1)
           MOVE 'KE'          TO ZC-ALLOW(1)
           MOVE 'US'          TO ZC-ALLOW(2)
           MOVE 'US-NY'       TO ZC-DENY(1)
           MOVE 'DUE '        TO ZC-K-KIND(1)
           MOVE 'T1'          TO ZC-K-ID(1)
           MOVE 1 TO ZC-K-PAYER(1)  MOVE 2 TO ZC-K-PAYEE(1)
           MOVE 'USD'         TO ZC-K-ASSET(1)
           MOVE 1200000       TO ZC-K-AMOUNT(1)
           MOVE 20261231      TO ZC-K-DATE(1)
           MOVE 12000         TO ZC-K-PENALTY(1)
           MOVE 'DUE '        TO ZC-K-KIND(2)
           MOVE 'T2'          TO ZC-K-ID(2)
           MOVE 1 TO ZC-K-PAYER(2)  MOVE 2 TO ZC-K-PAYEE(2)
           MOVE 'USD'         TO ZC-K-ASSET(2)
           MOVE 300000        TO ZC-K-AMOUNT(2)
           MOVE 20270331      TO ZC-K-DATE(2)
           MOVE 1             TO ZC-K-AFTER(2)
           MOVE 'GATE'        TO ZC-K-KIND(3)
           MOVE 'G1'          TO ZC-K-ID(3)
           MOVE 'ALL'         TO ZC-K-GOP(3)
           MOVE 2             TO ZC-K-NREF(3)
           MOVE 1 TO ZC-K-REF(3, 1)  MOVE 2 TO ZC-K-REF(3, 2)
           DISPLAY "C" ZXV-CONTRACT

      *    1. ACME pays BETA inside the window, both signed: ALLOW.
           INITIALIZE ZXV-TRANSFER
           MOVE 'ZXQ1'        TO ZQ-MAGIC
           MOVE 1 TO ZQ-FROM  MOVE 2 TO ZQ-TO
           MOVE 'USD'         TO ZQ-ASSET
           MOVE 1000000       TO ZQ-AMOUNT
           MOVE 20261101      TO ZQ-DATE
           MOVE 'US-DE'       TO ZQ-FROM-JUR
           MOVE 'KE'          TO ZQ-TO-JUR
           MOVE ALL 'N'       TO ZXV-TRANSFER(51:15)
           MOVE 'Y' TO ZQ-SIGNED(1)  MOVE 'Y' TO ZQ-SIGNED(2)
           MOVE ZC-DOC        TO ZQ-DOC
           PERFORM CHECK-ONE
           IF W-RC NOT = 0 OR ZP-DECISION NOT = 'ALLOW'
              OR ZP-VALUE NOT = 'T'
               DISPLAY "FAIL allow " W-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
      *    2. Inside the year-end lock: DENY on rule 2.
           MOVE 20261225      TO ZQ-DATE
           PERFORM CHECK-ONE
           IF W-RC NOT = 1 OR ZP-DECISION NOT = 'DENY '
              OR ZP-DENY(2) NOT = 'Y' OR ZP-ESCALATE NOT = 'N'
               DISPLAY "FAIL lock " W-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
      *    3. Above ACME's USD limit: DENY on rule 7.
           MOVE 20261101      TO ZQ-DATE
           MOVE 1600000       TO ZQ-AMOUNT
           PERFORM CHECK-ONE
           IF W-RC NOT = 1 OR ZP-DENY(7) NOT = 'Y'
               DISPLAY "FAIL limit " W-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
      *    4. Payee in US-NY: ALLOW US and DENY US-NY both cover it.
      *       The rule is BOTH, so the transfer is held for a person.
           MOVE 1000000       TO ZQ-AMOUNT
           MOVE 'US-NY'       TO ZQ-TO-JUR
           PERFORM CHECK-ONE
           IF W-RC NOT = 1 OR ZP-ESCALATE NOT = 'Y'
              OR ZP-VALUE NOT = 'B' OR ZP-BOTH(9) NOT = 'Y'
               DISPLAY "FAIL contested jurisdiction " W-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
      *    5. Only one signature: DENY on rule 3.
           MOVE 'KE'          TO ZQ-TO-JUR
           MOVE 'N' TO ZQ-SIGNED(2)
           PERFORM CHECK-ONE
           IF W-RC NOT = 1 OR ZP-DENY(3) NOT = 'Y'
               DISPLAY "FAIL signatures " W-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
      *    6. A malformed contract faults with the byte offset.
           MOVE 'Y' TO ZQ-SIGNED(2)
           MOVE 'k?'          TO ZC-LAW(1)
           PERFORM CHECK-ONE
           IF W-RC NOT = -7 OR ZP-DECISION NOT = 'FAULT'
              OR ZP-WHERE NOT = 137
               DISPLAY "FAIL fault " W-RC " " ZP-WHERE
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
           DISPLAY "OK"
           MOVE 0 TO RETURN-CODE
           STOP RUN.

       CHECK-ONE.
           CALL 'ZXV_POLICY' USING ZXV-CONTRACT ZXV-TRANSFER
               ZXV-POLICY-RESULT
           MOVE RETURN-CODE TO W-RC
           DISPLAY "Q" ZXV-TRANSFER
           DISPLAY "R" ZXV-POLICY-RESULT.
