      *> Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
      *> SPDX-License-Identifier: Apache-2.0
      *> payvalidate.cob — GnuCOBOL example that CALLs the ZEDEC legacy bridge
      *> C ABI (libzxlegacy) to decode a COMP-3 amount into an exact integer
      *> and re-encode it, proving the packed-decimal codec round-trips through
      *> the C library a batch job would link against.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. PAYVALIDATE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY txnrec.
       01  WS-LEN        PIC S9(9) COMP-5 VALUE 5.
       01  WS-DECODED    PIC S9(18) COMP-5.
       01  WS-RC         PIC S9(9)  COMP-5.
       01  WS-REENC      PIC S9(7)V99 COMP-3.
       01  WS-ABI        PIC S9(9)  COMP-5.
       PROCEDURE DIVISION.
       MAIN-PARA.
           CALL "zx_legacy_abi_version" RETURNING WS-ABI.
           DISPLAY "ZEDEC legacy ABI version: " WS-ABI.

      *> amount 12345.67 as an exact COMP-3 value (scaled integer 1234567)
           MOVE 12345.67 TO AMOUNT.
           CALL "zx_comp3_decode" USING
               BY REFERENCE AMOUNT
               BY VALUE WS-LEN
               BY REFERENCE WS-DECODED
               RETURNING WS-RC.
           IF WS-RC NOT = 0
               DISPLAY "decode failed rc=" WS-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF.
           DISPLAY "decoded scaled amount (x100): " WS-DECODED.

      *> re-encode the integer back into a COMP-3 field and compare
           CALL "zx_comp3_encode" USING
               BY VALUE WS-DECODED
               BY REFERENCE WS-REENC
               BY VALUE WS-LEN
               RETURNING WS-RC.
           IF WS-RC NOT = 0
               DISPLAY "encode failed rc=" WS-RC
               MOVE 1 TO RETURN-CODE
               STOP RUN
           END-IF.
           IF WS-REENC = AMOUNT
               DISPLAY "COMP-3 round trip OK: " WS-REENC
               MOVE 0 TO RETURN-CODE
           ELSE
               DISPLAY "COMP-3 MISMATCH"
               MOVE 1 TO RETURN-CODE
           END-IF.
           STOP RUN.
