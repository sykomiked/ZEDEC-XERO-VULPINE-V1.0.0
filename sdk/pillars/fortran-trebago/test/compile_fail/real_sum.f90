! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: rational_sum of REALs.
! expect: Type mismatch in argument 'v' .*passed REAL\(4\) to TYPE\(rational\)
program real_sum
  use zxv
  implicit none
  type(rational) :: r
  r = rational_sum([0.1, 0.2])
end program real_sum
