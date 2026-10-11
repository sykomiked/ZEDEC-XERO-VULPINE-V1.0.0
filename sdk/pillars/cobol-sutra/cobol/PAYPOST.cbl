      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      * SPDX-License-Identifier: Apache-2.0
      *
      * PAYPOST.CBL - a COBOL program that posts its own PAYMENT-REC
      * records to a libzxv ledger. The record goes to C BY REFERENCE,
      * exactly as the copybook lays it out; libzsutra reads it with the
      * same copybook (its path is the first argument) and posts it with a
      * LEDGER_POST capability. Refusals come back as status names.
      *
      *   cobc -x -fbinary-size=2-4-8 -fbinary-byteorder=big-endian
      *        -I examples cobol/PAYPOST.cbl cobol/zscob_shim.c ...
      *   ./paypost examples/payment.cpy        (see `make cobol`)
       IDENTIFICATION DIVISION.
       PROGRAM-ID. PAYPOST.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY "payment.cpy".
       01  CPY-PATH                PIC X(256).
       01  RC                      PIC S9(9)  COMP-5.
       01  SIGN-STYLE              PIC S9(9)  COMP-5 VALUE 1.
       01  ACCT-KIND               PIC S9(9)  COMP-5.
       01  ACCT-NAME               PIC X(30).
       01  ACCT-CCY                PIC X(3)   VALUE "NGN".
       01  BANK-ID                 PIC S9(9)  COMP-5.
       01  ALICE-ID                PIC S9(9)  COMP-5.
       01  BOB-ID                  PIC S9(9)  COMP-5.
       01  SHOW-ACCT               PIC S9(9)  COMP-5.
       01  ENTRY-ID                PIC S9(18) COMP-5.
       01  EQUITY                  PIC S9(18) COMP-5.
       01  STATUS-NAME             PIC X(24).
       01  FIELD-NAME              PIC X(30).
       01  AMOUNT-TEXT             PIC X(40).
       01  FEE-TEXT                PIC X(40).
       01  SHOW-NUM                PIC -(17)9.
       PROCEDURE DIVISION.
           ACCEPT CPY-PATH FROM ARGUMENT-VALUE
      *    SIGN-STYLE 1: GnuCOBOL's default ASCII overpunch
           CALL "zscob_open" USING CPY-PATH SIGN-STYLE RC
           IF RC NOT = 0
               DISPLAY "zscob_open failed: " RC
               STOP RUN
           END-IF
           CALL "zscob_currency" USING ACCT-CCY RC

           MOVE "bank" TO ACCT-NAME
           MOVE 1 TO ACCT-KIND
           CALL "zscob_account" USING ACCT-NAME ACCT-CCY ACCT-KIND
                BANK-ID RC
           MOVE "alice" TO ACCT-NAME
           MOVE 0 TO ACCT-KIND
           CALL "zscob_account" USING ACCT-NAME ACCT-CCY ACCT-KIND
                ALICE-ID RC
           MOVE "bob" TO ACCT-NAME
           CALL "zscob_account" USING ACCT-NAME ACCT-CCY ACCT-KIND
                BOB-ID RC

           PERFORM NEW-PAYMENT
           MOVE "FUND-ALICE"       TO PAY-REF
           MOVE 1000.00            TO PAY-AMOUNT
           MOVE BANK-ID            TO PAY-FROM
           MOVE ALICE-ID           TO PAY-TO
           PERFORM POST-PAYMENT

           PERFORM NEW-PAYMENT
           MOVE "PAY-0001"         TO PAY-REF
           MOVE 250.25             TO PAY-AMOUNT
           MOVE -1.50              TO PAY-FEE
           PERFORM POST-PAYMENT

           PERFORM NEW-PAYMENT
           MOVE "PAY-0002"         TO PAY-REF
           MOVE 5000.00            TO PAY-AMOUNT
           PERFORM POST-PAYMENT

           PERFORM NEW-PAYMENT
           MOVE "PAY-0001"         TO PAY-REF
           MOVE 1.00               TO PAY-AMOUNT
           PERFORM POST-PAYMENT

           PERFORM NEW-PAYMENT
           MOVE "PAY-0003"         TO PAY-REF
           MOVE -7.25              TO PAY-AMOUNT
           MOVE -12.34             TO PAY-FEE
           PERFORM POST-PAYMENT

           PERFORM NEW-PAYMENT
           MOVE "PAY-0004"         TO PAY-REF
           MOVE "USD"              TO PAY-CCY
           MOVE 10.00              TO PAY-AMOUNT
           PERFORM POST-PAYMENT

           MOVE BANK-ID TO SHOW-ACCT
           MOVE "bank" TO ACCT-NAME
           PERFORM SHOW-BALANCE
           MOVE ALICE-ID TO SHOW-ACCT
           MOVE "alice" TO ACCT-NAME
           PERFORM SHOW-BALANCE
           MOVE BOB-ID TO SHOW-ACCT
           MOVE "bob" TO ACCT-NAME
           PERFORM SHOW-BALANCE

           CALL "zscob_check" USING RC STATUS-NAME
           DISPLAY "ledger check: " FUNCTION TRIM(STATUS-NAME)
           CALL "zscob_close"
           STOP RUN.

       NEW-PAYMENT.
           MOVE SPACES TO PAYMENT-REC
           INITIALIZE PAYMENT-REC
           MOVE "NGN"              TO PAY-CCY
           MOVE ALICE-ID           TO PAY-FROM
           MOVE BOB-ID             TO PAY-TO
           MOVE 20261011           TO PAY-VALUE-DATE
           MOVE "N"                TO PAY-STATUS.

       POST-PAYMENT.
      *    libzsutra reads the amount and fee back from the bytes
           MOVE "PAY-AMOUNT" TO FIELD-NAME
           CALL "zscob_value" USING PAYMENT-REC FIELD-NAME AMOUNT-TEXT
                RC
           MOVE "PAY-FEE" TO FIELD-NAME
           CALL "zscob_value" USING PAYMENT-REC FIELD-NAME FEE-TEXT RC
           CALL "zscob_post" USING PAYMENT-REC ENTRY-ID RC STATUS-NAME
           IF RC = 0
               MOVE ENTRY-ID TO SHOW-NUM
               DISPLAY FUNCTION TRIM(PAY-REF) " "
                       FUNCTION TRIM(AMOUNT-TEXT) " " PAY-CCY
                       " (fee " FUNCTION TRIM(FEE-TEXT) ") -> entry "
                       FUNCTION TRIM(SHOW-NUM)
           ELSE
               DISPLAY FUNCTION TRIM(PAY-REF) " "
                       FUNCTION TRIM(AMOUNT-TEXT) " " PAY-CCY
                       " (fee " FUNCTION TRIM(FEE-TEXT) ") -> "
                       FUNCTION TRIM(STATUS-NAME)
           END-IF.

       SHOW-BALANCE.
           CALL "zscob_balance" USING SHOW-ACCT EQUITY RC
           MOVE EQUITY TO SHOW-NUM
           DISPLAY "equity " FUNCTION TRIM(ACCT-NAME) ": "
                   FUNCTION TRIM(SHOW-NUM) " kobo".
