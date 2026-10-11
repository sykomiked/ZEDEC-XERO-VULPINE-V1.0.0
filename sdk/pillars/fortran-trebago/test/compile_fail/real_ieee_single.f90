! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: the IEEE-754 boundary takes REAL(REAL64) only, never default REAL.
! expect: Type mismatch in argument 'x' .*passed REAL\(4\) to REAL\(8\)
program real_ieee_single
  use zxv
  implicit none
  type(rational) :: r
  r = rational_from_ieee754(0.1)
end program real_ieee_single
