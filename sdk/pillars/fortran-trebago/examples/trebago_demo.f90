! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! trebago_demo.f90 - exact arithmetic next to REAL(REAL64), for people who
! write numerical Fortran. Run with `make demo`.
!
! This demo is the one place in the pillar that computes with REAL on
! purpose, to show the difference; the modules themselves have no REAL
! outside rational_from_ieee754 (make check-noreal).
program trebago_demo
  use, intrinsic :: iso_fortran_env, only: int32, int64, real64
  use zxv
  use trebago
  implicit none
  character(len=1), parameter :: nl = new_line('a')

  call tenths()
  call cancellation()
  call hilbert()
  call growth()

contains

  function replace_nl(t) result(s)
    character(len=*), intent(in) :: t
    character(len=:), allocatable :: s
    integer :: i
    s = ''
    do i = 1, len(t)
      if (t(i:i) == nl) then
        if (i < len(t)) s = s // nl // '   '
      else
        s = s // t(i:i)
      end if
    end do
  end function

  ! 1. Ten tenths.
  subroutine tenths()
    real(real64) :: s
    type(rational) :: e
    integer :: i
    s = 0
    e = rational(0)
    do i = 1, 10
      s = s + 0.1_real64
      e = e + rational(1, 10)
    end do
    write (*, '(a)') '1. Ten times 0.1'
    write (*, '(a,es24.17,a,es9.2)') '   REAL(REAL64): ', s, '   minus 1: ', s - 1
    write (*, '(a,a,a,l1)') '   rational:     ', to_string(e), '                        == 1? ', e == 1
    write (*, '(a,a)') '   and the double 0.1 is exactly ', to_string(rational_from_ieee754(0.1_real64))
    write (*, '(a)') ''
  end subroutine

  ! 2. A dot product whose terms cancel: every input is an exact integer,
  !    the true answer is 1, and REAL(REAL64) returns 0.
  subroutine cancellation()
    type(trebago_program) :: p
    integer(int64), target :: xn(3), xd(3), yn(3), yd(3), sn, sd
    real(real64) :: s
    call p%load('examples/dot.tb')
    xn = [2_int64**53, 1_int64, -(2_int64**53)]
    xd = 1
    yn = 1
    yd = 1
    s = real(xn(1), real64) * 1 + real(xn(2), real64) * 1 + real(xn(3), real64) * 1
    call p%bind('x', xn, xd)
    call p%bind('y', yn, yd)
    call p%bind('s', sn, sd)
    call p%run()
    write (*, '(a)') '2. dot([2**53, 1, -2**53], [1, 1, 1]), the program examples/dot.tb:'
    write (*, '(a)') '   ' // replace_nl(p%text())
    write (*, '(a,f4.1)') '   REAL(REAL64): ', s
    write (*, '(a,i0,a,i0)') '   Trebago:      ', sn, '/', sd
    write (*, '(a)') ''
  end subroutine

  ! 3. The Hilbert matrix, exactly, and H x for x = (1, ..., 1).
  subroutine hilbert()
    type(trebago_program) :: p
    integer, parameter :: n = 8
    integer(int64), target :: xn(n), xd(n), hn(n, n), hd(n, n), bn(n), bd(n)
    real(real64) :: hr(n, n), br(n)
    integer :: i, j
    call p%parse('trebago hilbert' // nl // '  in x(n)' // nl // '  out h(n, n), b(n)' // nl // &
                 '  h(i, j) = 1 / (i + j - 1)' // nl // '  b(i) = sum(j, h(i, j) * x(j))' // nl // 'end')
    xn = 1
    xd = 1
    call p%bind('x', xn, xd)
    call p%bind('h', hn, hd)
    call p%bind('b', bn, bd)
    call p%run()
    do j = 1, n
      do i = 1, n
        hr(i, j) = 1.0_real64 / (i + j - 1)
      end do
    end do
    br = matmul(hr, [(1.0_real64, i = 1, n)])
    write (*, '(a,i0,a,i0,a)') '3. Hilbert ', n, 'x', n, ' times (1, ..., 1): exact row sums, and REAL(REAL64)'
    ! rational_from_ieee754 reads the REAL result exactly, so the comparison
    ! itself adds no rounding
    do i = 1, n
      write (*, '(a,i0,a,a14,a,es24.17,a,l1)') '   b(', i, ') = ', to_string(rational(bn(i), bd(i))), &
        '   REAL: ', br(i), '   exact? ', rational_from_ieee754(br(i)) == rational(bn(i), bd(i))
    end do
    write (*, '(a,i0,a)') '   (', trebago_last_work(), ' exact rational operations)'
    write (*, '(a)') ''
  end subroutine

  ! 4. The cost: denominators grow. H(n) = 1 + 1/2 + ... + 1/n has the
  !    denominator lcm(1..n); H(46) is the last that fits int64/int64.
  !    H(47) is refused with ZXV_E_RANGE - never rounded.
  subroutine growth()
    type(trebago_program) :: p
    integer(int64), target :: xn(47), xd(47), hn(47), hd(47)
    integer(int32) :: st
    integer :: k
    character(len=:), allocatable :: msg
    call p%parse('trebago harmonic' // nl // '  in x(n)' // nl // '  out h(n)' // nl // &
                 '  h(i) = scan(1 / i)' // nl // 'end')
    xn = 0
    xd = 1
    call p%bind('x', xn(1:46), xd(1:46))
    call p%bind('h', hn(1:46), hd(1:46))
    call p%run()
    write (*, '(a)') '4. Denominator growth: harmonic numbers H(n)'
    do k = 10, 34, 12
      write (*, '(a,i2,a,i0,a,i0)') '   H(', k, ') = ', hn(k), '/', hd(k)
    end do
    write (*, '(a,i0,a,i0)') '   H(46) = ', hn(46), '/', hd(46)
    call p%bind('x', xn, xd)
    call p%bind('h', hn, hd)
    call p%run(stat=st, message=msg)
    write (*, '(a,a)') '   H(47): ', trebago_status_name(st)
    write (*, '(a,a)') '   ', msg
  end subroutine

end program trebago_demo
