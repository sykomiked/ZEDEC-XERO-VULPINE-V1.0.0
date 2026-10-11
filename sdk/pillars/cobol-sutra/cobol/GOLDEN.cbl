      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      * SPDX-License-Identifier: Apache-2.0
      *
      * GOLDEN.CBL - writes the golden records in test/golden with
      * GnuCOBOL alone (no libzsutra), one hex line per record. Built
      * twice by `make golden-cobol`: with the default ASCII sign and
      * with -fsign=EBCDIC. The values are in test/golden/records.expect.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. GOLDEN.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY "payment.cpy".
       COPY "statement.cpy".
       01  REC-LEN                 PIC S9(9) COMP-5.
       01  REC-KIND                PIC X(12).
       01  I                       PIC 9(4) COMP.
       PROCEDURE DIVISION.
           MOVE SPACES TO PAYMENT-REC
           INITIALIZE PAYMENT-REC
           MOVE "PAY-0001"         TO PAY-REF
           MOVE 1234.56            TO PAY-AMOUNT
           MOVE "NGN"              TO PAY-CCY
           MOVE 1                  TO PAY-FROM
           MOVE 2                  TO PAY-TO
           MOVE 20261011           TO PAY-VALUE-DATE
           MOVE 1.50               TO PAY-FEE
           MOVE "N"                TO PAY-STATUS
           PERFORM SHOW-PAYMENT

           MOVE SPACES TO PAYMENT-REC
           INITIALIZE PAYMENT-REC
           MOVE "PAY-0002"         TO PAY-REF
           MOVE -0.01              TO PAY-AMOUNT
           MOVE "USD"              TO PAY-CCY
           MOVE 999999999          TO PAY-FROM
           MOVE 0                  TO PAY-TO
           MOVE 19991231           TO PAY-VALUE-DATE
           MOVE -12.34             TO PAY-FEE
           MOVE "P"                TO PAY-STATUS
           PERFORM SHOW-PAYMENT

           MOVE SPACES TO PAYMENT-REC
           INITIALIZE PAYMENT-REC
           MOVE "MAX-POSITIVE"     TO PAY-REF
           MOVE 9999999999999.99   TO PAY-AMOUNT
           MOVE "JPY"              TO PAY-CCY
           MOVE 65535              TO PAY-FROM
           MOVE 65536              TO PAY-TO
           MOVE 99999999           TO PAY-VALUE-DATE
           MOVE 99999.99           TO PAY-FEE
           MOVE "N"                TO PAY-STATUS
           PERFORM SHOW-PAYMENT

           MOVE SPACES TO PAYMENT-REC
           INITIALIZE PAYMENT-REC
           MOVE "MAX-NEGATIVE"     TO PAY-REF
           MOVE -9999999999999.99  TO PAY-AMOUNT
           MOVE "EUR"              TO PAY-CCY
           MOVE 7                  TO PAY-FROM
           MOVE 8                  TO PAY-TO
           MOVE 20000229           TO PAY-VALUE-DATE
           MOVE -99999.99          TO PAY-FEE
           MOVE "P"                TO PAY-STATUS
           PERFORM SHOW-PAYMENT

           MOVE SPACES TO PAYMENT-REC
           INITIALIZE PAYMENT-REC
           MOVE "ZEROS"            TO PAY-REF
           MOVE "NGN"              TO PAY-CCY
           MOVE 0.07               TO PAY-FEE
           PERFORM SHOW-PAYMENT

           INITIALIZE STATEMENT-REC
           MOVE 1234567890         TO ST-ACCOUNT
           MOVE 2026               TO ST-YEAR
           MOVE 10                 TO ST-MONTH
           MOVE 1000.00            TO ST-OPENING
           MOVE -250.75            TO ST-CLOSING
           MOVE 2                  TO ST-LINE-COUNT
           MOVE 20261001           TO ST-LINE-DATE (1)
           MOVE -1250.75           TO ST-LINE-AMOUNT (1)
           MOVE .000125            TO ST-LINE-RATE (1)
           MOVE "RENT"             TO ST-LINE-TEXT (1)
           MOVE 20261002           TO ST-LINE-DATE (2)
           MOVE 42.10              TO ST-LINE-AMOUNT (2)
           MOVE -.5                TO ST-LINE-RATE (2)
           MOVE "REFUND"           TO ST-LINE-TEXT (2)
      *    unused lines: zero by MOVE, so the sign convention applies
           PERFORM VARYING I FROM 3 BY 1 UNTIL I > 5
               MOVE 0 TO ST-LINE-DATE (I) ST-LINE-AMOUNT (I)
                         ST-LINE-RATE (I)
           END-PERFORM
           MOVE LENGTH OF STATEMENT-REC TO REC-LEN
           MOVE "statement"        TO REC-KIND
           CALL "golden_hex" USING BY REFERENCE REC-KIND
                BY REFERENCE STATEMENT-REC BY REFERENCE REC-LEN
           STOP RUN.

       SHOW-PAYMENT.
           MOVE LENGTH OF PAYMENT-REC TO REC-LEN
           MOVE "payment"          TO REC-KIND
           CALL "golden_hex" USING BY REFERENCE REC-KIND
                BY REFERENCE PAYMENT-REC BY REFERENCE REC-LEN.
