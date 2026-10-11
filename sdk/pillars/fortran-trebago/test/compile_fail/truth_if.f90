! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: a four-valued truth is not a LOGICAL.
! expect: IF clause at \(1\) requires a scalar LOGICAL expression
program truth_if
  use zxv
  implicit none
  type(truth) :: t
  t = TRUTH_BOTH
  if (t) print *, 'both'
end program truth_if
