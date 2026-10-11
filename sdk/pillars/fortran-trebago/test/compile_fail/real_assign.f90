! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: assigning a REAL to a rational.
! expect: Cannot convert REAL\(4\) to TYPE\(rational\)
program real_assign
  use zxv
  implicit none
  type(rational) :: r
  r = 0.25
end program real_assign
