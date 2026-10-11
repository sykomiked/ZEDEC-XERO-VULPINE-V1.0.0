      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
      * Genetics, LLC. SPDX-License-Identifier: Apache-2.0
      *
      * TRTYDEMO - a COBOL program that fills a ZXV treaty from the
      * copybook, runs it with CALL 'ZXV_ENGINE', and checks the
      * answer with its own PIC S9(18) arithmetic. It prints both
      * records so treaty_parity.c can compare them byte for byte
      * with the same treaty written in Sutra and run from C.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. TRTYDEMO.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY ZXVTRTY.
       COPY ZXVRSLT.
       01  W-RC         PIC S9(9).
       01  W-SUM        PIC S9(18).
       01  W-DEP        PIC S9(18).
       01  W-I          PIC 99.
       PROCEDURE DIVISION.
           DISPLAY "LEN " FUNCTION LENGTH(ZXV-TREATY) " "
               FUNCTION LENGTH(ZXV-RESULT)
           INITIALIZE ZXV-TREATY
           MOVE 'ZXT1'       TO ZT-MAGIC
           MOVE 'CORRIDOR-7' TO ZT-NAME
           MOVE 'USD'        TO ZT-ASSET
           MOVE 'SETL'       TO ZT-OP
           MOVE 3            TO ZT-PARTY-COUNT
           MOVE 3            TO ZT-OWE-COUNT
           MOVE 'ACME'       TO ZT-P-ID(1)
           MOVE 5000000      TO ZT-P-DEPOSIT(1)
           MOVE 4000         TO ZT-P-CLAIM(1)
           MOVE 1            TO ZT-P-WNUM(1)
           MOVE 3            TO ZT-P-WDEN(1)
           MOVE 'BETA'       TO ZT-P-ID(2)
           MOVE 3000000      TO ZT-P-DEPOSIT(2)
           MOVE 3000         TO ZT-P-CLAIM(2)
           MOVE 1            TO ZT-P-WNUM(2)
           MOVE 7            TO ZT-P-WDEN(2)
           MOVE 'GAMMA'      TO ZT-P-ID(3)
           MOVE 900000       TO ZT-P-DEPOSIT(3)
           MOVE 0            TO ZT-P-CLAIM(3)
           MOVE 11           TO ZT-P-WNUM(3)
           MOVE 21           TO ZT-P-WDEN(3)
           MOVE 1 TO ZT-O-FROM(1)  MOVE 2 TO ZT-O-TO(1)
           MOVE 1200000      TO ZT-O-AMOUNT(1)
           MOVE 2 TO ZT-O-FROM(2)  MOVE 3 TO ZT-O-TO(2)
           MOVE 700000       TO ZT-O-AMOUNT(2)
           MOVE 3 TO ZT-O-FROM(3)  MOVE 1 TO ZT-O-TO(3)
           MOVE 100000       TO ZT-O-AMOUNT(3)

           CALL 'ZXV_ENGINE' USING ZXV-TREATY ZXV-RESULT
           MOVE RETURN-CODE TO W-RC
           DISPLAY "RC " W-RC
           DISPLAY "T" ZXV-TREATY
           DISPLAY "R" ZXV-RESULT
           IF W-RC NOT = 0 OR ZR-STATUS NOT = 0 OR ZR-FEES NOT = 977
              OR ZR-LEG-COUNT NOT = 2 OR ZR-BALANCE(1) NOT = 3895023
              OR ZR-DIVIDEND(1) NOT = 81 OR ZR-DIVIDEND(3) NOT = 128
              DISPLAY "FAIL first call"
              MOVE 1 TO RETURN-CODE
              STOP RUN
           END-IF
      *    Conservation, in COBOL: deposits = balances + claims + fees.
           MOVE ZR-FEES TO W-SUM
           MOVE 0 TO W-DEP
           PERFORM VARYING W-I FROM 1 BY 1 UNTIL W-I > 3
               ADD ZR-BALANCE(W-I) ZT-P-CLAIM(W-I) TO W-SUM
                   ON SIZE ERROR DISPLAY "FAIL size"
               END-ADD
               ADD ZT-P-DEPOSIT(W-I) TO W-DEP
           END-PERFORM
           IF W-SUM NOT = W-DEP
               DISPLAY "FAIL conservation " W-SUM " " W-DEP
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF

      *    A claim above the deposit is refused, with the field offset.
           MOVE 5000001 TO ZT-P-CLAIM(1)
           CALL 'ZXV_ENGINE' USING ZXV-TREATY ZXV-RESULT
           MOVE RETURN-CODE TO W-RC
           IF W-RC NOT = -11 OR ZR-STATUS NOT = -11 OR ZR-WHERE NOT = 46
              OR ZR-FEES NOT = 0
               DISPLAY "FAIL claim > deposit " W-RC " " ZR-WHERE
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
      *    Weights that do not add up to 1 are refused.
           MOVE 4000 TO ZT-P-CLAIM(1)
           MOVE 2 TO ZT-P-WNUM(3)
           MOVE 21 TO ZT-P-WDEN(3)
           CALL 'ZXV_ENGINE' USING ZXV-TREATY ZXV-RESULT
           MOVE RETURN-CODE TO W-RC
           IF W-RC NOT = -10
               DISPLAY "FAIL weights " W-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF
           DISPLAY "OK"
           MOVE 0 TO RETURN-CODE
           STOP RUN.
