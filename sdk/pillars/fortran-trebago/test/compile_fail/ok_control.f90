! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! must compile: the exact counterparts of every case in this folder, so a
! failure above is the REAL (or the type confusion), not a broken setup.
program ok_control
  use, intrinsic :: iso_fortran_env, only: int64, real64
  use zxv
  use trebago
  implicit none
  type(rational) :: r
  type(truth) :: t
  type(trebago_program) :: p
  integer(int64), target :: xn(3), xd(3)
  integer :: i
  logical :: b
  r = rational(1, 2)
  r = rational(1, 3) + rational(1, 10)
  r = rational(1, 3) * 2
  r = rational(1, 2) / rational(1, 3)
  r = rational(1, 3) ** 2
  b = rational(1, 3) < rational(1, 2)
  r = 1
  r = rational_sum([rational(1, 10), rational(2, 10)])
  r = rational_from_ieee754(0.1_real64)
  r = rational_parse('0.25')
  xn = 1
  xd = 1
  call p%bind('x', xn, xd)
  t = TRUTH_BOTH
  if (t%to_logical(on_both=.false., on_neither=.false.)) print *, 'both'
  t = truth_of(1)
  i = int(numerator(r))
  print *, b, i
end program ok_control
