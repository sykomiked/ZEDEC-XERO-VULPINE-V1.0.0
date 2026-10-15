C     THE SAME THREE ALGORITHMS AS COLLATZ.BAS AND COLLATZ.FS:
C     COLLATZ STEPS FOR 1..12, EUCLID'S GCD, FIBONACCI NUMBERS.
      PROGRAM CROSS
      INTEGER N, K, STEPS, A, B, T, F1, F2, I
      DO 20 N = 1, 12
        K = N
        STEPS = 0
   10   IF (K .NE. 1) THEN
          IF (MOD(K, 2) .EQ. 0) THEN
            K = K / 2
          ELSE
            K = 3 * K + 1
          END IF
          STEPS = STEPS + 1
          GOTO 10
        END IF
        PRINT *, N, STEPS
   20 CONTINUE
      A = 1071
      B = 462
   30 IF (B .NE. 0) THEN
        T = MOD(A, B)
        A = B
        B = T
        GOTO 30
      END IF
      PRINT *, 'GCD', A
      F1 = 0
      F2 = 1
      DO 40 I = 1, 15
        PRINT *, F1
        T = F1 + F2
        F1 = F2
        F2 = T
   40 CONTINUE
      END
