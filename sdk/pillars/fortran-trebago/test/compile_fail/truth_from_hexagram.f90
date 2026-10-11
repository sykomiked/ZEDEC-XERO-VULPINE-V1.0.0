! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must NOT compile: a hexagram is not a truth.
! expect: Cannot convert TYPE\(hexagram\) to TYPE\(truth\)
program truth_from_hexagram
  use zxv
  implicit none
  type(truth) :: t
  t = hexagram(0, 0, 0)
end program truth_from_hexagram
