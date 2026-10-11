! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: a rational to a REAL power.
! expect: derived-type entities in binary intrinsic numeric operator|Operands of binary numeric operator
program real_pow
  use zxv
  implicit none
  type(rational) :: r
  r = rational(1, 3) ** 0.5
end program real_pow
