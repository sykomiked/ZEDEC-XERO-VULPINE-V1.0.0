       IDENTIFICATION DIVISION.
       PROGRAM-ID. CORE-BANKING-LEDGER.
       AUTHOR. 36N9 GENETICS LLC.
       DATE-WRITTEN. 2026-09-12.
       
       ENVIRONMENT DIVISION.
       CONFIGURATION SECTION.
       SOURCE-COMPUTER. ZXV-PQOS-ARM64.
       OBJECT-COMPUTER. ZXV-PQOS-ARM64.
       
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT LEDGER-FILE ASSIGN TO "LEDGER.DAT"
               ORGANIZATION IS INDEXED
               ACCESS MODE IS DYNAMIC
               RECORD KEY IS ACCOUNT-NUMBER
               ALTERNATE RECORD KEY IS CUSTOMER-ID WITH DUPLICATES.
       
       DATA DIVISION.
       FILE SECTION.
       FD  LEDGER-FILE.
       01  LEDGER-RECORD.
           05  ACCOUNT-NUMBER      PIC 9(16).
           05  CUSTOMER-ID         PIC 9(16).
           05  ACCOUNT-TYPE        PIC X(04).
               88  CHECKING        VALUE "CHCK".
               88  SAVINGS         VALUE "SAVE".
               88  INVESTMENT      VALUE "INV ".
           05  BALANCE             PIC S9(18)V99 COMP-3.
           05  CURRENCY-CODE       PIC X(03).
           05  LAST-TRANSACTION    PIC 9(14).
           05  LPRES-STATE         PIC 9(01).
               88  LPRES-TRUE      VALUE 1.
               88  LPRES-FALSE     VALUE 2.
               88  LPRES-BOTH      VALUE 3.
               88  LPRES-NEITHER   VALUE 0.
           05  M5-OMEGA            PIC 9(08).
           05  M5-R                PIC S9(8)V9(8) COMP-3.
           05  M5-ELL              PIC S9(8)V9(8) COMP-3.
           05  M5-PHI              PIC S9(8)V9(8) COMP-3.
           05  M5-CHI              PIC 9(08).
       
       WORKING-STORAGE SECTION.
       01  WS-TRANSACTION.
           05  TXN-TYPE            PIC X(10).
           05  TXN-AMOUNT          PIC S9(18)V99 COMP-3.
           05  TXN-FROM-ACCT       PIC 9(16).
           05  TXN-TO-ACCT         PIC 9(16).
           05  TXN-TIMESTAMP       PIC 9(14).
           05  TXN-LPRES           PIC 9(01).
       
       01  WS-M5-COORDS.
           05  WS-OMEGA            PIC 9(08).
           05  WS-R                PIC S9(8)V9(8) COMP-3.
           05  WS-ELL              PIC S9(8)V9(8) COMP-3.
           05  WS-PHI              PIC S9(8)V9(8) COMP-3.
           05  WS-CHI              PIC 9(08).
       
       01  WS-COVERAGE-RATIO       PIC S9(8)V9(8) COMP-3.
       01  WS-MIN-COVERAGE         PIC S9(8)V9(8) COMP-3 VALUE 1.8.
       
       01  WS-RETURN-CODE          PIC S9(04) COMP.
       
       LINKAGE SECTION.
       01  LS-ORBITAL-IR.
           05  LS-IR-FIELDS        OCCURS 8 TIMES.
               10  LS-FIELD-TYPE   PIC 9(01).
               10  LS-FIELD-NUM    PIC S9(18) COMP.
               10  LS-FIELD-DEN    PIC S9(18) COMP.
               10  LS-FIELD-SCALE  PIC 9(08).
       
       PROCEDURE DIVISION.
       MAIN-SECTION.
           PERFORM INITIALIZE-SYSTEM
           PERFORM PROCESS-TRANSACTIONS
           PERFORM SHUTDOWN-SYSTEM
           STOP RUN.
       
       INITIALIZE-SYSTEM.
           DISPLAY "CORE BANKING LEDGER INITIALIZING ON ZXV PQOS".
           CALL "ORBITAL_COMPAT_REGISTER" USING 
               BY VALUE 0  "COBOL/COMP-3" 
               BY REFERENCE WS-RETURN-CODE.
           IF WS-RETURN-CODE NOT = 0
               DISPLAY "ORBITAL COMPAT REGISTRATION FAILED: " WS-RETURN-CODE
               STOP RUN.
           
           MOVE 1 TO WS-MIN-COVERAGE.
           MOVE 1.8 TO WS-MIN-COVERAGE.
           
           CALL "M5_CARRIER_UP" USING BY REFERENCE WS-RETURN-CODE.
           CALL "LPRES_INIT" USING BY REFERENCE WS-RETURN-CODE.
           
           DISPLAY "CORE BANKING LEDGER READY".
       
       PROCESS-TRANSACTIONS.
           PERFORM UNTIL WS-TXN-TYPE = "SHUTDOWN"
               CALL "ORBITAL_LOWER" USING
                   BY VALUE 0
                   BY REFERENCE WS-TRANSACTION
                   BY VALUE 48
                   BY REFERENCE LS-ORBITAL-IR
                   BY REFERENCE WS-RETURN-CODE.
               
               IF WS-RETURN-CODE = 0
                   PERFORM VALIDATE-COVERAGE
                   IF WS-COVERAGE-RATIO >= WS-MIN-COVERAGE
                       PERFORM EXECUTE-TRANSACTION
                   ELSE
                       DISPLAY "COVERAGE INSUFFICIENT: " WS-COVERAGE-RATIO
                       MOVE 2 TO TXN-LPRES
                   END-IF
               ELSE
                   DISPLAY "ORBITAL LOWER FAILED: " WS-RETURN-CODE
                   MOVE 2 TO TXN-LPRES
               END-IF.
               
               CALL "ORBITAL_LIFT" USING
                   BY VALUE 0
                   BY REFERENCE LS-ORBITAL-IR
                   BY REFERENCE WS-TRANSACTION
                   BY VALUE 48
                   BY REFERENCE WS-RETURN-CODE.
           END-PERFORM.
       
       VALIDATE-COVERAGE.
           CALL "M5_COMPUTE_COVERAGE" USING
               BY REFERENCE WS-M5-COORDS
               BY REFERENCE WS-COVERAGE-RATIO
               BY REFERENCE WS-RETURN-CODE.
           
           CALL "LPRES_ATTEST" USING
               BY REFERENCE WS-TRANSACTION
               BY REFERENCE WS-RETURN-CODE.
       
       EXECUTE-TRANSACTION.
           EVALUATE TXN-TYPE
               WHEN "DEPOSIT"
                   PERFORM DEPOSIT-TO-ACCOUNT
               WHEN "WITHDRAW"
                   PERFORM WITHDRAW-FROM-ACCOUNT
               WHEN "TRANSFER"
                   PERFORM TRANSFER-BETWEEN-ACCOUNTS
               WHEN "BALANCE"
                   PERFORM QUERY-BALANCE
               WHEN "SETTLE"
                   PERFORM SETTLE-DERIVATIVE
               WHEN OTHER
                   DISPLAY "UNKNOWN TRANSACTION TYPE: " TXN-TYPE
                   MOVE 2 TO TXN-LPRES
           END-EVALUATE.
           
           MOVE 1 TO TXN-LPRES.
       
       DEPOSIT-TO-ACCOUNT.
           MOVE TXN-TO-ACCT TO ACCOUNT-NUMBER.
           READ LEDGER-FILE RECORD.
           IF WS-RETURN-CODE = 0
               ADD TXN-AMOUNT TO BALANCE
               MOVE FUNCTION CURRENT-DATE(1:14) TO LAST-TRANSACTION
               MOVE 1 TO LPRES-STATE
               REWRITE LEDGER-RECORD
           ELSE
               DISPLAY "ACCOUNT NOT FOUND: " TXN-TO-ACCT
               MOVE 2 TO LPRES-STATE
           END-IF.
       
       WITHDRAW-FROM-ACCOUNT.
           MOVE TXN-FROM-ACCT TO ACCOUNT-NUMBER.
           READ LEDGER-FILE RECORD.
           IF WS-RETURN-CODE = 0
               IF BALANCE >= TXN-AMOUNT
                   SUBTRACT TXN-AMOUNT FROM BALANCE
                   MOVE FUNCTION CURRENT-DATE(1:14) TO LAST-TRANSACTION
                   MOVE 1 TO LPRES-STATE
                   REWRITE LEDGER-RECORD
               ELSE
                   DISPLAY "INSUFFICIENT FUNDS: " TXN-FROM-ACCT
                   MOVE 3 TO LPRES-STATE
               END-IF
           ELSE
               DISPLAY "ACCOUNT NOT FOUND: " TXN-FROM-ACCT
               MOVE 2 TO LPRES-STATE
           END-IF.
       
       TRANSFER-BETWEEN-ACCOUNTS.
           MOVE TXN-FROM-ACCT TO ACCOUNT-NUMBER.
           READ LEDGER-FILE RECORD.
           IF WS-RETURN-CODE = 0
               IF BALANCE >= TXN-AMOUNT
                   SUBTRACT TXN-AMOUNT FROM BALANCE
                   MOVE TXN-TO-ACCT TO ACCOUNT-NUMBER
                   READ LEDGER-FILE RECORD
                   IF WS-RETURN-CODE = 0
                       ADD TXN-AMOUNT TO BALANCE
                       MOVE FUNCTION CURRENT-DATE(1:14) TO LAST-TRANSACTION
                       MOVE 1 TO LPRES-STATE
                       REWRITE LEDGER-RECORD
                   ELSE
                       DISPLAY "DESTINATION ACCOUNT NOT FOUND"
                       MOVE 2 TO LPRES-STATE
                   END-IF
               ELSE
                   DISPLAY "INSUFFICIENT FUNDS FOR TRANSFER"
                   MOVE 3 TO LPRES-STATE
               END-IF
           ELSE
               DISPLAY "SOURCE ACCOUNT NOT FOUND"
               MOVE 2 TO LPRES-STATE
           END-IF.
       
       QUERY-BALANCE.
           MOVE TXN-FROM-ACCT TO ACCOUNT-NUMBER.
           READ LEDGER-FILE RECORD.
           IF WS-RETURN-CODE = 0
               DISPLAY "BALANCE FOR " ACCOUNT-NUMBER ": " BALANCE
               MOVE 1 TO LPRES-STATE
           ELSE
               DISPLAY "ACCOUNT NOT FOUND: " TXN-FROM-ACCT
               MOVE 2 TO LPRES-STATE
           END-IF.
       
       SETTLE-DERIVATIVE.
           DISPLAY "DERIVATIVE SETTLEMENT NOT YET IMPLEMENTED IN COBOL"
           MOVE 3 TO LPRES-STATE.
       
       SHUTDOWN-SYSTEM.
           DISPLAY "CORE BANKING LEDGER SHUTTING DOWN".
           CALL "ORBITAL_COMPAT_UNREGISTER" USING
               BY VALUE 0
               BY REFERENCE WS-RETURN-CODE.
