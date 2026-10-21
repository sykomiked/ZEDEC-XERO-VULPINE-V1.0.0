C Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
C SPDX-License-Identifier: Apache-2.0
C     A TOKEN AMOUNT IN ZECKENDORF FORM (KITLING_RULES.H KR3, SWARM_DENOM.H
C     D1 AND D3): BIT K OF THE ANSWER IS SET WHEN TIER K, OF FACE F(K+2),
C     HOLDS A TOKEN, FOUND GREEDILY FROM THE TOP TIER DOWN. AN INTEGER
C     HERE IS 32 BITS, SO THE MASK HOLDS 31 TIERS: AN AMOUNT PAST 3524577
C     (F(33) - 1) OR BELOW 0 IS REFUSED.
      PROGRAM ZECK
      INTEGER IV, IA, IB, IT, K, M, IP
      READ *, IV
      IF (IV .LT. 0 .OR. IV .GT. 3524577) THEN
        PRINT *, 'REFUSED'
        STOP
      END IF
      IA = 1
      IB = 2
      K = 0
      IP = 1
      DO WHILE (IB .LE. IV)
        IT = IA + IB
        IA = IB
        IB = IT
        K = K + 1
        IP = IP * 2
      END DO
      M = 0
      DO WHILE (K .GE. 0)
        IF (IA .LE. IV) THEN
          IV = IV - IA
          M = M + IP
        END IF
        IT = IB - IA
        IB = IA
        IA = IT
        K = K - 1
        IF (K .GE. 0) IP = IP / 2
      END DO
      PRINT *, M
      END
