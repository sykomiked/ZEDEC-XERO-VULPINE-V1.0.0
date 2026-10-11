! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: comparing a rational with a REAL.
! expect: Operands of comparison operator '<' at \(1\) are TYPE\(rational\)/REAL
program real_compare
  use zxv
  implicit none
  logical :: t
  t = rational(1, 3) < 0.5
end program real_compare
