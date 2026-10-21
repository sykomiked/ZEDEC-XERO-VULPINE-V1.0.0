C Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
C SPDX-License-Identifier: Apache-2.0
C     THE DAY OF THE WEEK OF A GREGORIAN DATE (SOLSTICE_RULES.H SO2):
C     READS YEAR, MONTH AND DAY, PRINTS 1 FOR MONDAY .. 7 FOR SUNDAY, BY
C     ZELLER'S CONGRUENCE (JANUARY AND FEBRUARY COUNTED AS MONTHS 13 AND
C     14 OF THE YEAR BEFORE). THE DAY MUST BE IN ITS MONTH. AN INTEGER
C     HERE IS 32 BITS, SO THE YEAR IS HELD TO NINE DIGITS: A YEAR PAST
C     999999999 OR BELOW 1 IS REFUSED.
      PROGRAM WKDAY
      INTEGER IY, M, ID, LAST, K, IH
      READ *, IY, M, ID
      IF (IY .LT. 1 .OR. IY .GT. 999999999) THEN
        PRINT *, 'REFUSED'
        STOP
      END IF
      IF (M .LT. 1 .OR. M .GT. 12) THEN
        PRINT *, 'REFUSED'
        STOP
      END IF
      LAST = 30 + MOD(M + M / 8, 2)
      IF (M .EQ. 2) THEN
        LAST = 28
        IF (MOD(IY, 4) .EQ. 0 .AND. MOD(IY, 100) .NE. 0) LAST = 29
        IF (MOD(IY, 400) .EQ. 0) LAST = 29
      END IF
      IF (ID .LT. 1 .OR. ID .GT. LAST) THEN
        PRINT *, 'REFUSED'
        STOP
      END IF
      K = IY
      IF (M .LT. 3) THEN
        M = M + 12
        K = K - 1
      END IF
      IH = ID + 13 * (M + 1) / 5 + K + K / 4 - K / 100 + K / 400
      IH = MOD(IH, 7)
      PRINT *, MOD(IH + 5, 7) + 1
      END
