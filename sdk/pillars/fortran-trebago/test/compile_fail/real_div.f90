! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: a REAL divided by a rational.
! expect: derived-type entities in binary intrinsic numeric operator|Operands of binary numeric operator
program real_div
  use zxv
  implicit none
  type(rational) :: r
  r = 0.5 / rational(1, 3)
end program real_div
