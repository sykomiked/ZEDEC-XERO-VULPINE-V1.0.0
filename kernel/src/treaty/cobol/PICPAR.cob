      * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
      * Genetics, LLC. SPDX-License-Identifier: Apache-2.0
      *
      * PICPAR - the COBOL half of the PIC S9(18) parity test
      * (pic_parity.c writes the vectors and compares the answers).
      * Reads vec.txt: op (A S M D), a, b as PIC S9(18) SIGN LEADING
      * SEPARATE. Writes out.txt: 'K' and the result (and remainder
      * for D), or 'E' when COBOL raised ON SIZE ERROR.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. PICPAR.
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT VEC ASSIGN TO "vec.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
           SELECT ANS ASSIGN TO "out.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
       DATA DIVISION.
       FILE SECTION.
       FD  VEC.
       01  V-REC.
           05  V-OP     PIC X.
           05  V-A      PIC S9(18) SIGN LEADING SEPARATE.
           05  V-B      PIC S9(18) SIGN LEADING SEPARATE.
       FD  ANS.
       01  A-REC.
           05  A-FLAG   PIC X.
           05  A-R      PIC S9(18) SIGN LEADING SEPARATE.
           05  A-REM    PIC S9(18) SIGN LEADING SEPARATE.
       WORKING-STORAGE SECTION.
       01  W-EOF        PIC X VALUE 'N'.
       01  W-R          PIC S9(18) SIGN LEADING SEPARATE.
       01  W-REM        PIC S9(18) SIGN LEADING SEPARATE.
       PROCEDURE DIVISION.
           OPEN INPUT VEC OUTPUT ANS
           PERFORM UNTIL W-EOF = 'Y'
               READ VEC
                   AT END MOVE 'Y' TO W-EOF
                   NOT AT END PERFORM ONE-VECTOR
               END-READ
           END-PERFORM
           CLOSE VEC ANS
           STOP RUN.
       ONE-VECTOR.
           MOVE 'K' TO A-FLAG
           MOVE 0 TO W-R W-REM
           EVALUATE V-OP
               WHEN 'A'
                   ADD V-A V-B GIVING W-R
                       ON SIZE ERROR MOVE 'E' TO A-FLAG
                   END-ADD
               WHEN 'S'
                   SUBTRACT V-B FROM V-A GIVING W-R
                       ON SIZE ERROR MOVE 'E' TO A-FLAG
                   END-SUBTRACT
               WHEN 'M'
                   MULTIPLY V-A BY V-B GIVING W-R
                       ON SIZE ERROR MOVE 'E' TO A-FLAG
                   END-MULTIPLY
               WHEN 'D'
                   DIVIDE V-A BY V-B GIVING W-R REMAINDER W-REM
                       ON SIZE ERROR MOVE 'E' TO A-FLAG
                   END-DIVIDE
               WHEN OTHER
                   MOVE '?' TO A-FLAG
           END-EVALUATE
           IF A-FLAG NOT = 'K'
               MOVE 0 TO W-R W-REM
           END-IF
           MOVE W-R TO A-R
           MOVE W-REM TO A-REM
           WRITE A-REC.
