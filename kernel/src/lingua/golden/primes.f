C     PRIMES AND SUMS IN FORTRAN 77
      PROGRAM PRIMES
      INTEGER N, I, J, ISP, S, A(0:9)
      PARAMETER (N = 50)
      DO 20 I = 2, N
         ISP = 1
         DO 10 J = 2, I - 1
            IF (MOD(I, J) .EQ. 0) ISP = 0
   10    CONTINUE
         IF (ISP .EQ. 1) PRINT *, I
   20 CONTINUE
      S = 0
      DO 30 I = 1, 10
   30 S = S + I**2
      PRINT *, 'SUM OF SQUARES', S, 'AFTER LOOP I =', I
      DO I = 9, 0, -1
        A(I) = I * I
      END DO
      PRINT *, A(0), A(9), MAX(3, 7, -2), ISIGN(5, -1), -7/2, 2**(-1)
      I = 0
  40  I = I + 1
      IF (I .LT. 3) GOTO 40
      IF (I - 3) 50, 60, 50
  50  PRINT *, 'BAD'
  60  PRINT *, 'ARITH IF OK'
      GO TO (70, 80), 2
  70  PRINT *, 'BAD2'
  80  IF (I .GT. 2 .AND. .NOT. I .EQ. 5) THEN
        PRINT *, 'BLOCK IF'
      ELSE IF (I == 3) THEN
        PRINT *, 'NO'
      ELSE
        PRINT *, 'NO2'
      END IF
      J = 1
      DO WHILE (J .LT. 100)
        J = J * 3
      ENDDO
      WRITE (*,*) J, 'DONE', 'X'
      PRINT *, 'IT''S', 
     &  12345
      END
