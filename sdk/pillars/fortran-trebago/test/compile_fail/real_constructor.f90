! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: rational(0.5): there is no REAL constructor.
! expect: PRIVATE component of 'rational'|no specific function for the generic 'rational'
program real_constructor
  use zxv
  implicit none
  type(rational) :: r
  r = rational(0.5)
end program real_constructor
