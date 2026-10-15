C Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
C SPDX-License-Identifier: Apache-2.0
C     GREATEST COMMON DIVISOR BY EUCLID: READS A AND B, PRINTS GCD(A, B).
      PROGRAM GCD
      INTEGER IA, IB, IT
      READ *, IA, IB
      IA = ABS(IA)
      IB = ABS(IB)
      DO WHILE (IB .NE. 0)
        IT = MOD(IA, IB)
        IA = IB
        IB = IT
      END DO
      PRINT *, IA
      END
