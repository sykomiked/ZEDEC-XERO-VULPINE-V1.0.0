! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! test_trebago.f90 - module trebago from Fortran: parse, bind, run, and the
! results checked against module zxv's own rational arithmetic (sum, dot,
! scale, prefix sum, matrix-vector product, Hilbert matrix); refusals with
! stat= (overflow, work limit, shape, division by zero, syntax) leave the
! output arrays untouched; canonical text and digest; copying a program.
! Run from sdk/pillars/fortran-trebago (it loads examples/*.tb).
program test_trebago
  use, intrinsic :: iso_fortran_env, only: int32, int64
  use zxv
  use trebago
  implicit none
  integer :: checks = 0, fails = 0
  integer(int64) :: rng = 2463534242_int64
  character(len=1), parameter :: nl = new_line('a')

  call test_dot_and_sum()
  call test_matvec()
  call test_kernels_file()
  call test_scale_and_prefix()
  call test_hilbert()
  call test_overflow_untouched()
  call test_refusals()
  call test_identity_and_copy()

  write (*, '(a,i0,a,i0,a)') 'test_trebago: ', checks, ' checks, ', fails, ' failed'
  if (fails > 0) error stop 1

contains

  subroutine check(cond, what)
    logical, intent(in) :: cond
    character(len=*), intent(in) :: what
    checks = checks + 1
    if (.not. cond) then
      fails = fails + 1
      write (*, '(a)') 'FAIL: ' // what
    end if
  end subroutine

  ! a small xorshift, so the run is the same every time
  integer(int64) function rand_in(lo, hi)
    integer(int64), intent(in) :: lo, hi
    rng = ieor(rng, ishft(rng, 13))
    rng = ieor(rng, ishft(rng, -7))
    rng = ieor(rng, ishft(rng, 17))
    rand_in = lo + modulo(rng, hi - lo + 1)
  end function

  logical function is(r, n, d)
    type(rational), intent(in) :: r
    integer(int64), intent(in) :: n, d
    is = numerator(r) == n .and. denominator(r) == d
  end function

  ! random rationals with numerators in [-big, big] and denominators in [1, dmax]
  subroutine random_rationals(v, big, dmax)
    type(rational), intent(out) :: v(:)
    integer(int64), intent(in) :: big, dmax
    integer :: i
    do i = 1, size(v)
      v(i) = rational(rand_in(-big, big), rand_in(1_int64, dmax))
    end do
  end subroutine

  ! Trebago's dot and sum against rational_dot and rational_sum, including
  ! inputs large enough that both refuse with ZXV_E_RANGE at the same time.
  subroutine test_dot_and_sum()
    type(trebago_program) :: p
    type(rational), allocatable :: x(:), y(:)
    integer(int64), allocatable, target :: xn(:), xd(:), yn(:), yd(:)
    integer(int64), target :: sn, sd, tn, td
    type(rational) :: want_dot, want_sum
    integer(int32) :: st, st_dot, st_sum
    integer :: trial, n, agreed, refused
    integer(int64) :: big, dmax
    call p%parse('trebago ds' // nl // '  in x(n), y(n)' // nl // '  out s, t' // nl // &
                 '  s = sum(i, x(i) * y(i))' // nl // '  t = sum(i, x(i))' // nl // 'end')
    call check(p%is_parsed(), 'parse from a string')
    agreed = 0
    refused = 0
    do trial = 1, 600
      n = int(rand_in(0_int64, 24_int64))
      big = merge(1000_int64, 2_int64**40, trial <= 400)
      dmax = merge(12_int64, 997_int64, trial <= 400)
      allocate(x(n), y(n), xn(n), xd(n), yn(n), yd(n))
      call random_rationals(x, big, dmax)
      call random_rationals(y, big, dmax)
      call rational_split(x, xn, xd)
      call rational_split(y, yn, yd)
      call p%bind('x', xn, xd)
      call p%bind('y', yn, yd)
      call p%bind('s', sn, sd)
      call p%bind('t', tn, td)
      sn = -7
      sd = 1
      want_dot = rational_dot(x, y, st_dot)
      want_sum = rational_sum(x, st_sum)
      call p%run(stat=st)
      if (st_dot == ZXV_OK .and. st_sum == ZXV_OK) then
        call check(st == ZXV_OK .and. is(want_dot, sn, sd) .and. is(want_sum, tn, td), 'dot and sum agree')
        agreed = agreed + 1
      else
        call check(st == ZXV_E_RANGE .and. sn == -7, 'dot or sum overflow is refused, s untouched')
        refused = refused + 1
      end if
      deallocate(x, y, xn, xd, yn, yd)
    end do
    call check(agreed > 300 .and. refused > 20, 'both exact results and refusals were exercised')
    write (*, '(a,i0,a,i0,a)') 'dot/sum: ', agreed, ' agreed exactly, ', refused, ' refused alike'
  end subroutine

  ! y = a x with a rectangular a(m, n), bound as a Fortran 2-D array.
  subroutine test_matvec()
    type(trebago_program) :: p
    integer(int64), allocatable, target :: an(:, :), ad(:, :), xn(:), xd(:), yn(:), yd(:)
    type(rational), allocatable :: a(:, :), x(:)
    type(rational) :: acc
    integer :: trial, m, n, i, j
    logical :: ok
    call p%load('examples/matvec.tb')
    do trial = 1, 50
      m = int(rand_in(1_int64, 7_int64))
      n = int(rand_in(1_int64, 9_int64))
      allocate(a(m, n), x(n), an(m, n), ad(m, n), xn(n), xd(n), yn(m), yd(m))
      do j = 1, n
        call random_rationals(a(:, j), 50_int64, 12_int64)
        call rational_split(a(:, j), an(:, j), ad(:, j))
      end do
      call random_rationals(x, 50_int64, 12_int64)
      call rational_split(x, xn, xd)
      call p%bind('a', an, ad)
      call p%bind('x', xn, xd)
      call p%bind('y', yn, yd)
      call p%run()
      ok = .true.
      do i = 1, m
        acc = rational(0)
        do j = 1, n
          acc = acc + a(i, j) * x(j)
        end do
        ok = ok .and. is(acc, yn(i), yd(i))
      end do
      call check(ok, 'matvec equals the Fortran loop')
      deallocate(a, x, an, ad, xn, xd, yn, yd)
    end do
  end subroutine

  ! examples/kernels.tb, the five kernels in one program.
  subroutine test_kernels_file()
    type(trebago_program) :: p
    integer(int64), target :: xn(3), xd(3), yn(3), yd(3), an, ad, zn(3), zd(3)
    integer(int64), target :: totn, totd, dotn, dotd, pn(3), pd(3), hn(3, 3), hd(3, 3), hxn(3), hxd(3)
    integer :: i, j
    logical :: ok
    call p%load('examples/kernels.tb')
    xn = [1, 2, 3]
    xd = 1
    yn = 1
    yd = [2, 4, 8]
    an = -2
    ad = 3
    zn = 1
    zd = 1
    call p%bind('x', xn, xd)
    call p%bind('y', yn, yd)
    call p%bind('alpha', an, ad)
    call p%bind('z', zn, zd)
    call p%bind('total', totn, totd)
    call p%bind('dot', dotn, dotd)
    call p%bind('prefix', pn, pd)
    call p%bind('h', hn, hd)
    call p%bind('hx', hxn, hxd)
    call p%run()
    call check(totn == 6 .and. totd == 1, 'kernels: total = 6')
    call check(dotn == 11 .and. dotd == 8, 'kernels: dot = 11/8')
    call check(all(zn == [1, -1, -1]) .and. all(zd == [3, 3, 1]), 'kernels: z = alpha*x + z, in place')
    call check(all(pn == [1, 1, 2]) .and. all(pd == [3, 1, 1]), 'kernels: prefix = 1/3, 1, 2')
    ok = .true.
    do j = 1, 3
      do i = 1, 3
        ok = ok .and. hn(i, j) == 1 .and. hd(i, j) == i + j - 1
      end do
    end do
    call check(ok, 'kernels: h(i, j) = 1/(i+j-1)')
    call check(all(hxn == [3, 23, 43]) .and. all(hxd == [1, 12, 30]), 'kernels: h x = 3, 23/12, 43/30')
    call check(trebago_last_work() > 0, 'the work counter moved')
  end subroutine

  subroutine test_scale_and_prefix()
    type(trebago_program) :: p
    integer(int64), target :: xn(40), xd(40), an, ad, yn(40), yd(40), sn(40), sd(40)
    type(rational) :: x(40), y(40), run
    integer :: i
    logical :: ok_y, ok_s
    call p%parse('trebago sp' // nl // '  in x(n), alpha' // nl // '  out y(n), s(n)' // nl // &
                 '  y(i) = alpha * x(i)' // nl // '  s(i) = scan(y(i))' // nl // 'end')
    call random_rationals(x, 10000_int64, 60_int64)
    call rational_split(x, xn, xd)
    an = -5
    ad = 7
    call p%bind('x', xn, xd)
    call p%bind('alpha', an, ad)
    call p%bind('y', yn, yd)
    call p%bind('s', sn, sd)
    call p%run()
    ok_y = .true.
    ok_s = .true.
    run = rational(0)
    do i = 1, 40
      y(i) = rational(-5, 7) * x(i)
      run = run + y(i)
      ok_y = ok_y .and. is(y(i), yn(i), yd(i))
      ok_s = ok_s .and. is(run, sn(i), sd(i))
    end do
    call check(ok_y, 'scale y = alpha x')
    call check(ok_s, 'prefix sum of y')
  end subroutine

  ! The Hilbert matrix is the classic case floating point gets wrong: here
  ! it is exact, and H * (1, ..., 1) is checked entry by entry.
  subroutine test_hilbert()
    type(trebago_program) :: p
    integer, parameter :: n = 10
    integer(int64), target :: xn(n), xd(n), hn(n, n), hd(n, n), bn(n), bd(n)
    type(rational) :: acc
    integer :: i, j
    logical :: ok
    call p%parse('trebago hilbert' // nl // '  in x(n)' // nl // '  out h(n, n), b(n)' // nl // &
                 '  h(i, j) = 1 / (i + j - 1)' // nl // '  b(i) = sum(j, h(i, j) * x(j))' // nl // 'end')
    xn = 1
    xd = 1
    call p%bind('x', xn, xd)
    call p%bind('h', hn, hd)
    call p%bind('b', bn, bd)
    call p%run()
    ok = .true.
    do i = 1, n
      acc = rational(0)
      do j = 1, n
        ok = ok .and. hn(i, j) == 1 .and. hd(i, j) == i + j - 1
        acc = acc + rational(1, i + j - 1)
      end do
      ok = ok .and. is(acc, bn(i), bd(i))
    end do
    call check(ok, 'Hilbert 10x10 and its row sums')
    call check(bn(1) == 7381 .and. bd(1) == 2520, 'row 1 of H10 sums to H(10) = 7381/2520')
  end subroutine

  ! Harmonic numbers: H(46) fits in int64/int64, H(47) does not. The run
  ! is refused with ZXV_E_RANGE and the output keeps what it held.
  subroutine test_overflow_untouched()
    type(trebago_program) :: p
    integer(int64), target :: xn(47), xd(47), hn(47), hd(47)
    integer(int32) :: st
    character(len=:), allocatable :: msg
    call p%parse('trebago harmonic' // nl // '  in x(n)' // nl // '  out h(n)' // nl // &
                 '  h(i) = scan(1 / i)' // nl // 'end')
    xn = 0
    xd = 1
    hn = 42
    hd = 1
    call p%bind('x', xn(1:46), xd(1:46))
    call p%bind('h', hn(1:46), hd(1:46))
    call p%run(stat=st)
    call check(st == ZXV_OK .and. hn(46) == 5943339269060627227_int64 .and. hd(46) == 1345655451257488800_int64, &
      'H(46) exactly')
    hn = 42
    hd = 1
    call p%bind('x', xn, xd)
    call p%bind('h', hn, hd)
    call p%run(stat=st, message=msg)
    call check(st == ZXV_E_RANGE, 'H(47) is refused with ZXV_E_RANGE')
    call check(all(hn == 42) .and. all(hd == 1), 'a refused run leaves the outputs untouched')
    call check(index(msg, 'ZXV_E_RANGE') > 0 .and. index(msg, 'i=47') > 0, 'the message names the status and i=47: ' // msg)
  end subroutine

  subroutine test_refusals()
    type(trebago_program) :: p
    integer(int64), target :: xn(3), xd(3), yn(4), yd(4), sn, sd, qn(3), qd(3)
    integer(int32) :: st
    character(len=:), allocatable :: msg
    ! syntax: line and column in the message
    call p%parse('trebago bad' // nl // '  in x(n)' // nl // '  out s' // nl // '  s = sum(i, x(i) *)' // nl // 'end', &
                 stat=st, message=msg)
    call check(st == TB_E_SYNTAX .and. .not. p%is_parsed(), 'syntax error refused')
    call check(index(msg, 'line 4') > 0, 'diagnostic has the line: ' // msg)
    ! a REAL literal is refused by the parser, not rounded
    call p%parse('trebago r' // nl // '  out s' // nl // '  s = 1.0e-3' // nl // 'end', stat=st)
    call check(st == TB_E_SYNTAX, 'exponent notation refused')
    call p%load('examples/broken.tb', stat=st)
    call check(st == TB_E_SYNTAX, 'examples/broken.tb refused')
    call p%load('examples/no-such-file.tb', stat=st)
    call check(st == ZXV_E_IO, 'a missing file is ZXV_E_IO')
    ! shapes
    call p%load('examples/dot.tb')
    xn = 1
    xd = 1
    yn = 1
    yd = 1
    sn = 99
    sd = 1
    call p%bind('x', xn, xd)
    call p%bind('y', yn, yd)
    call p%bind('s', sn, sd)
    call p%run(stat=st)
    call check(st == TB_E_SHAPE .and. sn == 99, 'x(3) and y(4) disagree on n')
    ! a missing binding
    call p%clear_bindings()
    call p%bind('x', xn, xd)
    call p%bind('s', sn, sd)
    call p%run(stat=st)
    call check(st == TB_E_NAME, 'an unbound parameter is TB_E_NAME')
    ! a zero denominator in an input is refused, not divided
    call p%bind('y', yn(1:3), yd(1:3))
    yd(2) = 0
    call p%run(stat=st)
    call check(st /= ZXV_OK .and. sn == 99, 'a zero input denominator is refused')
    yd = 1
    ! division by zero
    call p%parse('trebago q' // nl // '  in x(n)' // nl // '  out q(n)' // nl // '  q(i) = 1 / (x(i) - 2)' // nl // 'end')
    xn = [1, 2, 3]
    qn = 5
    qd = 1
    call p%bind('x', xn, xd)
    call p%bind('q', qn, qd)
    call p%run(stat=st, message=msg)
    call check(st == TB_E_DIVZERO .and. all(qn == 5), 'division by zero refused, q untouched: ' // msg)
    ! the work limit
    call p%parse('trebago w' // nl // '  in x(n)' // nl // '  out s' // nl // &
                 '  s = sum(i, sum(j = 1:1000, x(i) * j))' // nl // 'end')
    call p%bind('x', xn, xd)
    call p%bind('s', sn, sd)
    call p%set_work_limit(500_int64)
    call p%run(stat=st, message=msg)
    call check(st == TB_E_LIMIT .and. sn == 99, 'the work limit refuses with TB_E_LIMIT: ' // msg)
    call check(trebago_status_name(TB_E_LIMIT) == 'TB_E_LIMIT', 'status name')
    call p%set_work_limit(0_int64)
    call p%run(stat=st, message=msg)
    call check(st == ZXV_OK, 'with the default limit it runs: ' // msg)
    ! no program
    call p%destroy()
    call p%run(stat=st)
    call check(st == ZXV_E_ARG .and. .not. p%is_parsed(), 'run without a program')
  end subroutine

  subroutine test_identity_and_copy()
    type(trebago_program) :: a, b, c
    integer(int64), target :: xn(2), xd(2), yn(2), yd(2), sn, sd
    integer(int32) :: st
    call a%load('examples/dot.tb')
    call b%load('test/golden/dot_spelled.tb')
    call check(a%digest() == b%digest() .and. a%text() == b%text(), &
      'two spellings of the dot product have one canonical text and digest')
    call check(len_trim(a%digest()) == 64 .and. verify(a%digest(), '0123456789abcdef') == 0, 'digest is 64 hex digits')
    call check(index(a%text(), 'trebago dot') == 1, 'canonical text: ' // a%text())
    c = a
    call check(c%is_parsed() .and. c%digest() == a%digest(), 'assignment copies the program')
    call a%destroy()
    call check(.not. a%is_parsed() .and. c%is_parsed(), 'the copy is independent')
    xn = [1, 3]
    xd = [2, 4]
    yn = [2, 4]
    yd = [3, 5]
    call c%run(stat=st)
    call check(st == TB_E_NAME, 'bindings are not copied')
    call c%bind('X', xn, xd)
    call c%bind('Y', yn, yd)
    call c%bind('S', sn, sd)
    call c%run()
    call check(sn == 14 .and. sd == 15, 'names bind case-insensitively; 1/3 + 3/5 = 14/15')
  end subroutine

end program test_trebago
