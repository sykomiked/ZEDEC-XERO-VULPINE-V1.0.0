C Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
C SPDX-License-Identifier: Apache-2.0
C     THE DAY NUMBER OF A GREGORIAN DATE (ORRERY_RULES.H OR1): READS
C     YEAR, MONTH AND DAY, PRINTS THE DAYS SINCE 1970-01-01 (A DATE
C     BEFORE IT IS NEGATIVE). THE YEAR IS COUNTED FROM MARCH, SO THE
C     LEAP DAY ENDS IT; 400 YEARS ARE 146097 DAYS. YEARS 1 TO 9999 ONLY,
C     AND THE DAY MUST BE IN ITS MONTH: ANYTHING ELSE IS REFUSED.
      PROGRAM CIVDAY
      INTEGER IY, M, ID, LAST, Y, ERA, YOE, MP, DOY, DOE
      READ *, IY, M, ID
      IF (IY .LT. 1 .OR. IY .GT. 9999) THEN
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
      Y = IY
      MP = M - 3
      IF (M .LE. 2) THEN
        Y = IY - 1
        MP = M + 9
      END IF
      ERA = Y / 400
      YOE = Y - ERA * 400
      DOY = (153 * MP + 2) / 5 + ID - 1
      DOE = YOE * 365 + YOE / 4 - YOE / 100 + DOY
      PRINT *, ERA * 146097 + DOE - 719468
      END
