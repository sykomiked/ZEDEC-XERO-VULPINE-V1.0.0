! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: binding REAL arrays to a Trebago program.
! expect: no matching specific binding for the call to the GENERIC 'bind'
program real_bind
  use trebago
  implicit none
  type(trebago_program) :: p
  double precision, target :: xn(3), xd(3)
  call p%bind('x', xn, xd)
end program real_bind
