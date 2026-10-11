! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: no silent conversion of a rational to an INTEGER.
! expect: Cannot convert TYPE\(rational\) to INTEGER
program rational_to_integer
  use zxv
  implicit none
  integer :: i
  i = rational(7, 2)
end program rational_to_integer
