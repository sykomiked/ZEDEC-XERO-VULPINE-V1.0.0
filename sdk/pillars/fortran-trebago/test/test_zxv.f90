! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! test_zxv.f90 - module zxv against sdk/conformance/vectors.json (through
! the generated module conformance_vectors): status names and classes,
! rationals (operators, stat forms, parse, IEEE-754, Q16), four-valued
! logic, every hexagram table, the quarantine log chain, capabilities, the
! ledger, and handle lifetimes (R6).
program test_zxv
  use, intrinsic :: iso_fortran_env, only: int8, int32, int64, real64
  use, intrinsic :: iso_c_binding, only: c_int32_t, c_int64_t
  use zxv
  use conformance_vectors
  implicit none
  integer :: checks = 0, fails = 0
  integer(int64) :: rng = 88172645463325252_int64

  ! the C comparison, to check the module's own (pure) one against it
  interface
    integer(c_int32_t) function c_rat_cmp(an, ad, bn, bd, out) bind(c, name='zxv_rat_cmp')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: an, ad, bn, bd
      integer(c_int32_t), intent(out) :: out
    end function
  end interface

  call test_library()
  call test_status()
  call test_rational_vectors()
  call test_rational_api()
  call test_compare_differential()
  call test_truth()
  call test_hexagram()
  call test_quarantine()
  call test_capabilities_and_ledger()
  call test_lifetimes()

  write (*, '(a,i0,a,i0,a)') 'test_zxv: ', checks, ' checks, ', fails, ' failed'
  if (fails > 0) error stop 1

contains

  subroutine check(cond, what)
    logical, intent(in) :: cond
    character(len=*), intent(in) :: what
    checks = checks + 1
    if (.not. cond) then
      fails = fails + 1
      write (*, '(a)') 'FAIL: ' // what // ' [last error: ' // last_error() // ']'
    end if
  end subroutine

  function i2s(i) result(s)
    integer(int64), intent(in) :: i
    character(len=:), allocatable :: s
    character(len=24) :: b
    write (b, '(i0)') i
    s = trim(b)
  end function

  logical function is(r, n, d)
    type(rational), intent(in) :: r
    integer(int64), intent(in) :: n, d
    is = numerator(r) == n .and. denominator(r) == d
  end function

  integer(int64) function next_random()
    rng = ieor(rng, ishft(rng, 13))
    rng = ieor(rng, ishft(rng, -7))
    rng = ieor(rng, ishft(rng, 17))
    next_random = rng
  end function

  ! The module compares in pure Fortran (continued fractions, no 128-bit
  ! products); libzxv compares in C. They must agree everywhere, including
  ! huge numerators and denominators and the integer -2**63.
  subroutine test_compare_differential()
    type(rational) :: a, b
    integer(int64) :: an, ad, bn, bd, m
    integer(int64), parameter :: MIN64 = -huge(1_int64) - 1_int64
    integer(int64) :: special(7)
    integer(int32) :: st, c, st2
    integer :: i, k, bad, done
    special = [MIN64, -huge(1_int64), -1_int64, 0_int64, 1_int64, huge(1_int64), 3037000499_int64]
    bad = 0
    done = 0
    do i = 1, 20000
      an = ishft(next_random(), -int(iand(next_random(), 63_int64)))
      if (iand(next_random(), 1_int64) == 1) an = -an
      ad = max(1_int64, ishft(iand(next_random(), huge(1_int64)), -int(iand(next_random(), 63_int64))))
      bn = ishft(next_random(), -int(iand(next_random(), 63_int64)))
      if (iand(next_random(), 1_int64) == 1) bn = -bn
      bd = max(1_int64, ishft(iand(next_random(), huge(1_int64)), -int(iand(next_random(), 63_int64))))
      if (mod(i, 7) == 0) then
        bn = an + iand(next_random(), 3_int64) - 1   ! near-equal pairs
        if (bn == MIN64) bn = an
        bd = ad
      end if
      a = rational_from_parts(an, ad, st)
      b = rational_from_parts(bn, bd, st2)
      if (st /= ZXV_OK .or. st2 /= ZXV_OK) cycle
      st = c_rat_cmp(numerator(a), denominator(a), numerator(b), denominator(b), c)
      if (st /= ZXV_OK) then
        bad = bad + 1
        cycle
      end if
      done = done + 1
      if ((a < b) .neqv. (c < 0)) bad = bad + 1
      if ((a == b) .neqv. (c == 0)) bad = bad + 1
      if ((a >= b) .neqv. (c >= 0)) bad = bad + 1
      do k = 1, size(special)
        m = special(k)
        st = c_rat_cmp(numerator(a), denominator(a), m, 1_int64, c)
        if (st /= ZXV_OK) cycle
        if ((a < m) .neqv. (c < 0)) bad = bad + 1
        if ((m > a) .neqv. (c < 0)) bad = bad + 1
        if ((a == m) .neqv. (c == 0)) bad = bad + 1
      end do
    end do
    checks = checks + done - 1
    call check(bad == 0 .and. done > 15000, 'pure comparison agrees with zxv_rat_cmp on ' // i2s(int(done, int64)) // &
      ' random pairs and the int64 extremes')
    call check(rational(-3, 1) > MIN64 .and. MIN64 < 0 .and. .not. (rational(0) < MIN64), 'compare with -2**63')
  end subroutine

  subroutine test_library()
    integer(int32) :: ma, mi, pa, st, mask
    call zxv_abi_version(ma, mi, pa)
    call check(ma == 1 .and. mi >= 3, 'ABI 1.3 or a later 1.x')
    call zxv_require_abi(st)
    call check(st == ZXV_OK, 'zxv_require_abi')
    mask = zxv_feature_mask()
    call check(iand(mask, ZXV_FEAT_RATIONAL) /= 0 .and. iand(mask, ZXV_FEAT_HEX) /= 0, &
      'features: rationals and hexagrams')
  end subroutine

  subroutine test_status()
    integer :: i
    do i = 1, V_STATUS_N
      call check(status_name(V_STATUS_CODE(i)) == trim(V_STATUS_NAME(i)), 'status name ' // trim(V_STATUS_NAME(i)))
      call check(status_class(V_STATUS_CODE(i)) == V_STATUS_CLASS(i), 'status class ' // trim(V_STATUS_NAME(i)))
    end do
    call check(status_class(-12345) == ZXV_CLASS_SYSTEM, 'an unknown code is SYSTEM')
  end subroutine

  subroutine test_rational_vectors()
    type(rational) :: a, b, r
    integer :: i
    integer(int32) :: st, q
    integer(int64) :: n, d
    do i = 1, V_RAT_N
      a = rational(V_RAT_A(1, i), V_RAT_A(2, i))
      b = rational(V_RAT_B(1, i), V_RAT_B(2, i))
      call check(is(a + b, V_RAT_ADD(1, i), V_RAT_ADD(2, i)), 'add ' // to_string(a) // ' ' // to_string(b))
      call check(is(a - b, V_RAT_SUB(1, i), V_RAT_SUB(2, i)), 'sub ' // to_string(a))
      call check(is(a * b, V_RAT_MUL(1, i), V_RAT_MUL(2, i)), 'mul ' // to_string(a))
      call check(is(a / b, V_RAT_DIV(1, i), V_RAT_DIV(2, i)), 'div ' // to_string(a))
      call rational_add(a, b, r, st)
      call check(st == ZXV_OK .and. is(r, V_RAT_ADD(1, i), V_RAT_ADD(2, i)), 'rational_add')
      call rational_sub(a, b, r, st)
      call check(st == ZXV_OK .and. is(r, V_RAT_SUB(1, i), V_RAT_SUB(2, i)), 'rational_sub')
      call rational_mul(a, b, r, st)
      call check(st == ZXV_OK .and. is(r, V_RAT_MUL(1, i), V_RAT_MUL(2, i)), 'rational_mul')
      call rational_div(a, b, r, st)
      call check(st == ZXV_OK .and. is(r, V_RAT_DIV(1, i), V_RAT_DIV(2, i)), 'rational_div')
      select case (V_RAT_CMP(i))
      case (-1)
        call check(a < b .and. a <= b .and. a /= b .and. .not. (a >= b), 'cmp -1')
      case (0)
        call check(a == b .and. a <= b .and. a >= b, 'cmp 0')
      case default
        call check(a > b .and. a >= b .and. a /= b .and. .not. (a <= b), 'cmp +1')
      end select
    end do
    do i = 1, V_PARSE_N
      r = rational_parse(trim(V_PARSE_TEXT(i)), st)
      call check(st == V_PARSE_STATUS(i), 'parse status "' // trim(V_PARSE_TEXT(i)) // '"')
      if (V_PARSE_STATUS(i) == ZXV_OK) then
        call check(is(r, V_PARSE_VALUE(1, i), V_PARSE_VALUE(2, i)), 'parse value ' // trim(V_PARSE_TEXT(i)))
        call check(to_string(r) == trim(V_PARSE_FORMAT(i)), 'format ' // trim(V_PARSE_FORMAT(i)))
      end if
    end do
    do i = 1, V_IEEE_N
      r = rational_from_ieee754_bits(V_IEEE_BITS(i), st)
      call check(st == V_IEEE_STATUS(i), 'ieee754 status ' // i2s(V_IEEE_BITS(i)))
      if (st == ZXV_OK) call check(is(r, V_IEEE_VALUE(1, i), V_IEEE_VALUE(2, i)), 'ieee754 value')
      ! the same through the REAL(REAL64) boundary
      r = rational_from_ieee754(transfer(V_IEEE_BITS(i), 1.0_real64), st)
      call check(st == V_IEEE_STATUS(i), 'rational_from_ieee754 status')
      if (st == ZXV_OK) call check(is(r, V_IEEE_VALUE(1, i), V_IEEE_VALUE(2, i)), 'rational_from_ieee754 value')
    end do
    do i = 1, V_Q16_N
      n = V_Q16_VALUE(1, i)
      d = V_Q16_VALUE(2, i)
      r = rational(n, d)
      q = r%to_q16(st)
      call check(q == merge(V_Q16_Q(i), 0, V_Q16_STATUS(i) == ZXV_OK) .and. st == V_Q16_STATUS(i), &
        'q16 of ' // to_string(r))
    end do
  end subroutine

  subroutine test_rational_api()
    type(rational) :: a, b, r, v(4), w(4)
    integer(int32) :: st
    integer(int64) :: num(4), den(4)
    character(len=40) :: buf
    a = rational(1, 3)
    b = 2
    call check(is(b, 2_int64, 1_int64), 'assignment from an integer')
    b = 5_int64
    call check(is(a + 1, 4_int64, 3_int64) .and. is(1 - a, 2_int64, 3_int64), 'mixed with int32')
    call check(is(a * 3_int64, 1_int64, 1_int64) .and. is(2_int64 / a, 6_int64, 1_int64), 'mixed with int64')
    call check(a < 1 .and. 0 < a .and. a /= 0 .and. rational(2, 6) == a .and. a <= 1_int64, 'compare with integers')
    call check(is(-a, -1_int64, 3_int64) .and. is(abs(-a), 1_int64, 3_int64), 'negation and abs')
    call check(is(a ** 3, 1_int64, 27_int64) .and. is(a ** (-2), 9_int64, 1_int64) .and. is(a ** 0, 1_int64, 1_int64), &
      'integer powers')
    call check(is(rational(-1) ** 63_int64, -1_int64, 1_int64) .and. is(rational(-1) ** (-huge(1_int64) - 1), &
      1_int64, 1_int64), 'powers of -1')
    call check(is(rational(6, -4), -3_int64, 2_int64), 'normalized: denominator positive, reduced')
    call check(is(rational(2_int64) ** 62, 4611686018427387904_int64, 1_int64), '2 ** 62 fits')
    ! refusals with stat: nothing changes
    r = rational(7)
    call rational_add(rational(huge(1_int64)), rational(1), r, st)
    call check(st == ZXV_E_RANGE .and. is(r, 7_int64, 1_int64), 'overflow is ZXV_E_RANGE, r unchanged')
    call rational_mul(rational(3037000500_int64), rational(3037000500_int64), r, st)
    call check(st == ZXV_E_RANGE, '3037000500 squared does not fit')
    call rational_div(a, rational(0), r, st)
    call check(st == ZXV_E_ARG .and. is(r, 7_int64, 1_int64), 'division by zero is ZXV_E_ARG')
    r = rational_from_parts(1_int64, 0_int64, st)
    call check(st == ZXV_E_ARG, 'denominator 0')
    r = rational_from_parts(-huge(1_int64) - 1, 1_int64, st)
    call check(st /= ZXV_OK, 'INT64_MIN is refused')
    r = rational_parse('1e5', st)
    call check(st == ZXV_E_FORMAT, 'no exponents')
    ! arrays
    v = [rational(1, 2), rational(1, 3), rational(1, 6), rational(-1)]
    call check(is(rational_sum(v), 0_int64, 1_int64), 'rational_sum')
    w = [rational(2), rational(3), rational(6), rational(1)]
    call check(is(rational_dot(v, w), 2_int64, 1_int64), 'rational_dot')
    call check(is(rational_dot(v(1:3), w(1:3)), 3_int64, 1_int64), 'rational_dot on sections')
    r = rational_dot(v, w(1:3), st)
    call check(st == ZXV_E_ARG, 'rational_dot of different lengths')
    v(1) = rational(huge(1_int64))
    v(2) = rational(1)
    r = rational_sum(v(1:2), st)
    call check(st == ZXV_E_RANGE, 'rational_sum overflow with stat')
    call rational_split([rational(1, 2), rational(-3, 4)], num(1:2), den(1:2))
    call check(all(num(1:2) == [1_int64, -3_int64]) .and. all(den(1:2) == [2_int64, 4_int64]), 'rational_split')
    w(1:2) = rational_join([2_int64, 6_int64], [4_int64, -9_int64])
    call check(is(w(1), 1_int64, 2_int64) .and. is(w(2), -2_int64, 3_int64), 'rational_join normalizes')
    ! the DT edit descriptor and list-directed output use the exact text
    write (buf, '(DT)') rational(-51, 4)
    call check(trim(buf) == '-12.75', 'write (DT): ' // trim(buf))
    write (buf, '(DT)') rational(1, 3)
    call check(trim(buf) == '1/3', 'write (DT) 1/3')
    ! 0.1 as a double is not 1/10, and the module says so
    r = rational_from_ieee754(0.1_real64)
    call check(r /= rational(1, 10) .and. is(r, 3602879701896397_int64, 36028797018963968_int64), &
      'the double nearest 0.1 is exactly 3602879701896397/36028797018963968')
  end subroutine

  subroutine test_truth()
    integer :: a, b
    type(truth) :: t(0:3), x
    integer(int32) :: st
    t = [TRUTH_NEITHER, TRUTH_TRUE, TRUTH_FALSE, TRUTH_BOTH]
    do a = 0, 3
      call check(t(a)%value() == a .and. t(a)%name() == trim(V_LP_NAME(a)), 'truth ' // trim(V_LP_NAME(a)))
      x = .not. t(a)
      call check(x%value() == V_LP_NOT(a), 'not ' // trim(V_LP_NAME(a)))
      do b = 0, 3
        x = t(a) .and. t(b)
        call check(x%value() == V_LP_AND(a, b), 'and ' // trim(V_LP_NAME(a)) // ' ' // trim(V_LP_NAME(b)))
        x = t(a) .or. t(b)
        call check(x%value() == V_LP_OR(a, b), 'or ' // trim(V_LP_NAME(a)) // ' ' // trim(V_LP_NAME(b)))
      end do
      call check(truth_of(a) == t(a), 'truth_of')
    end do
    call check(truth_from_evidence(2_int64, 0_int64) == TRUTH_TRUE .and. truth_from_evidence(0_int64, 1_int64) == &
      TRUTH_FALSE .and. truth_from_evidence(1_int64, 1_int64) == TRUTH_BOTH .and. truth_from_evidence(0_int64, 0_int64) &
      == TRUTH_NEITHER, 'truth_from_evidence')
    call check(TRUTH_TRUE%is_certainly_true() .and. .not. TRUTH_BOTH%is_certainly_true() .and. TRUTH_BOTH%is_both() &
      .and. TRUTH_FALSE%is_certainly_false() .and. TRUTH_NEITHER%is_neither(), 'predicates')
    call check(TRUTH_BOTH%to_logical(on_both=.false., on_neither=.false.) .eqv. .false., 'policy for BOTH')
    call check(TRUTH_NEITHER%to_logical(on_both=.false., on_neither=.true.), 'policy for NEITHER')
    call check(TRUTH_TRUE%to_logical(.false., .false.) .and. .not. TRUTH_FALSE%to_logical(.true., .true.), &
      'TRUE and FALSE ignore the policy')
    t(0) = truth_of(4, st)
    call check(st == ZXV_E_ARG, 'truth_of(4) is refused')
  end subroutine

  subroutine test_hexagram()
    type(hexagram) :: h, g, r, x
    integer :: t, u, p, k
    integer(int32) :: st
    do t = 0, 63
      h = hexagram_from_tag(t)
      call check(h%tag() == t .and. h%name() == trim(V_HEX_NAME(t)), 'hex name ' // trim(V_HEX_NAME(t)))
      call check(h%base() == V_HEX_BASE(t) .and. h%polarity() == V_HEX_POLARITY(t) .and. h%phase() == V_HEX_PHASE(t), &
        'hex fields')
      call check(h%action() == V_HEX_ACTION(t) .and. h%action_name() == trim(V_HEX_ACTION_NAME(V_HEX_ACTION(t))), &
        'hex action ' // trim(V_HEX_NAME(t)))
      call check(hexagram(V_HEX_BASE(t), V_HEX_POLARITY(t), V_HEX_PHASE(t)) == h, 'hexagram(base, polarity, phase)')
      x = .not. h
      call check(x%tag() == V_HEX_NOT(t), 'hex not')
      call check(h%is_forbidden() .eqv. (V_HEX_POLARITY(t) == HEX_POL_FORBIDDEN), 'is_forbidden')
      x = h%forbid()
      call check(x%is_forbidden() .and. x%phase() == h%phase(), 'forbid keeps the phase')
      do u = 0, 63
        g = hexagram_from_tag(u)
        x = h .and. g
        if (x%tag() /= V_HEX_AND(t, u)) call check(.false., 'hex and')
        x = h .or. g
        if (x%tag() /= V_HEX_OR(t, u)) call check(.false., 'hex or')
      end do
      checks = checks + 2 * 64
      do p = 0, 3
        r = h%step(p, st)
        if (V_HEX_STEP(t, p) < 0) then
          call check(st == ZXV_E_STATE .and. r == h, 'step refused')
        else
          call check(st == ZXV_OK .and. r%tag() == V_HEX_STEP(t, p), 'step')
        end if
      end do
      x = h%advance()
      call check(x%phase() == V_HEX_PHASE_NEXT(h%phase()), 'advance')
      do k = 0, 15
        r = h%with_state(k / 4, mod(k, 4), st)
        if (V_HEX_SET(t, k) < 0) then
          call check(st == ZXV_E_STATE, 'with_state refused')
        else
          call check(st == ZXV_OK .and. r%tag() == V_HEX_SET(t, k), 'with_state')
        end if
      end do
    end do
    do k = 0, 3
      x = hexagram_from_truth(truth_of(k), HEX_POL_NEUTRAL, HEX_PHASE_POTENTIAL)
      call check(x%base() == V_HEX_LP_TO_BASE(k), 'truth -> base')
      x = hexagram(k, 0, 0)
      call check(x%to_truth() == truth_of(V_HEX_BASE_TO_LP(k)), 'base -> truth')
      call check(hex_phase_next(k) == V_HEX_PHASE_NEXT(k), 'phase_next')
      call check(to_string(hexagram(k, 1, 0)) == trim(V_HEX_BASE_NAME(k)) // '/' // trim(V_HEX_POL_NAME(1)) // '/' // &
        trim(V_HEX_PHASE_NAME(0)), 'name parts')
    end do
    do k = 1, V_POL_N
      call check(hex_polarity_from_evidence(V_POL_POS0(k), V_POL_NEG0(k), V_POL_POS1(k), V_POL_NEG1(k), V_POL_STALE(k)) &
        == V_POL_RESULT(k), 'polarity from evidence')
    end do
    h = hexagram_from_tag(64, st)
    call check(st == ZXV_E_ARG, 'tag 64 is refused')
    h = hexagram(4, 0, 0, st)
    call check(st == ZXV_E_ARG, 'base 4 is refused')
    h = hexagram(HEX_BASE_TRUE, HEX_POL_NEGENTROPIC, HEX_PHASE_MANIFEST)
    call check(h%action() == HEX_ACT_PROCEED, 'true/negentropic proceeds')
    g = h%forbid()
    x = g .and. h
    r = .not. g
    call check(g%action() == HEX_ACT_ROLLBACK .and. x%is_forbidden() .and. r%is_forbidden(), &
      'forbidden is absorbing')
    r = g%with_state(HEX_BASE_TRUE, HEX_POL_NEUTRAL, st)
    call check(st == ZXV_E_STATE .and. r == g, 'a forbidden tag cannot be un-forbidden')
  end subroutine

  subroutine test_quarantine()
    type(context) :: ctx
    type(capability) :: root, attest, reader
    type(quarantine_log) :: log
    integer :: i
    integer(int32) :: st, reason
    integer(int64) :: seq, sub
    integer(int8) :: subject(32), prev(32), hash(32)
    type(hexagram) :: tag
    call ctx%create()
    call ctx%root_capability(root)
    call root%attenuate(CAP_ATTEST, attest)
    call root%attenuate(CAP_LEDGER_READ, reader)
    call log%create(reader, 4, st)
    call check(st == ZXV_E_CAP, 'a log needs ATTEST')
    call log%create(attest, V_QLOG_N)
    call check(log%count() == 0 .and. log%head() == repeat('0', 64), 'empty log')
    do i = 1, V_QLOG_N
      call log%append(attest, hexagram_from_tag(V_QLOG_TAG(i)), V_QLOG_REASON(i), V_QLOG_SUBMITTER(i), &
        V_QLOG_SUBJECT(:, i), seq)
      call check(seq == V_QLOG_SEQ(i), 'seq')
    end do
    call check(log%count() == V_QLOG_N, 'count')
    call check(log%head() == V_QLOG_HEAD, 'the head equals vectors.json')
    do i = 1, V_QLOG_N
      call log%record(i - 1, seq, tag, reason, sub, subject, prev, hash)
      call check(seq == V_QLOG_SEQ(i) .and. tag%tag() == V_QLOG_TAG(i) .and. reason == V_QLOG_REASON(i) .and. &
        sub == V_QLOG_SUBMITTER(i) .and. all(subject == V_QLOG_SUBJECT(:, i)), 'record fields')
      call check(bytes_to_hex(prev) == V_QLOG_PREV(i) .and. bytes_to_hex(hash) == V_QLOG_HASH(i), 'record chain')
    end do
    call log%verify()
    call check(.true., 'verify')
    call log%append(attest, hexagram_from_tag(59), 1, 1_int64, subject, seq, st)
    call check(st == ZXV_E_CAPACITY, 'a full log is ZXV_E_CAPACITY')
    call log%append(attest, hexagram(HEX_BASE_TRUE, HEX_POL_NEUTRAL, 0), 1, 1_int64, subject, seq, st)
    call check(st == ZXV_E_ARG, 'only forbidden tags are quarantined')
    call log%append(reader, hexagram_from_tag(59), 1, 1_int64, subject, seq, st)
    call check(st == ZXV_E_CAP, 'appending needs ATTEST')
  end subroutine

  subroutine test_capabilities_and_ledger()
    type(context) :: ctx
    type(capability) :: root, poster, reader
    type(ledger) :: admin, books, viewer
    integer(int32) :: bank, alice, bob, st
    integer(int64) :: e1, e2, eq_bank, eq_alice, eq_bob, debit, credit
    call ctx%create()
    call ctx%root_capability(root, CAP_ALL)
    call root%attenuate(ior(CAP_LEDGER_POST, CAP_LEDGER_READ), poster)
    call poster%attenuate(CAP_LEDGER_READ, reader)
    call check(poster%bits() == ior(CAP_LEDGER_POST, CAP_LEDGER_READ) .and. reader%bits() == CAP_LEDGER_READ, 'bits')
    call check(reader%check(CAP_LEDGER_READ) .and. .not. reader%check(CAP_LEDGER_POST), 'check')
    call admin%init(root)
    call books%init(poster)
    call viewer%init(reader)
    call admin%enable_currency('NGN')
    call admin%open_account('bank', 'NGN', ACCOUNT_ISSUER, bank)
    call admin%open_account('alice', 'NGN', ACCOUNT_HOLDER, alice)
    call admin%open_account('bob', 'NGN', ACCOUNT_HOLDER, bob)
    call books%post(bank, alice, 100000_int64, 'NGN', 'FUND-ALICE', e1)
    call books%post(alice, bob, 25025_int64, 'NGN', 'PAY-0001', e2)
    call check(e1 == 1 .and. e2 == 2, 'entry ids')
    call books%post(alice, bob, 500000_int64, 'NGN', 'PAY-0002', stat=st)
    call check(st == ZXV_E_FUNDS, 'insufficient funds')
    call books%post(alice, bob, 1_int64, 'NGN', 'PAY-0001', stat=st)
    call check(st == ZXV_E_DUPLICATE, 'duplicate reference')
    call books%post(alice, bob, 0_int64, 'NGN', 'PAY-0003', stat=st)
    call check(st == ZXV_E_RANGE, 'amount 0')
    call books%post(alice, bob, 1_int64, 'USD', 'PAY-0004', stat=st)
    call check(st == ZXV_E_CURRENCY, 'USD is not enabled')
    call viewer%post(alice, bob, 1_int64, 'NGN', 'PAY-0005', stat=st)
    call check(st == ZXV_E_CAP, 'a reader cannot post')
    call viewer%balance(bank, eq_bank)
    call viewer%balance(alice, eq_alice, debit, credit)
    call viewer%balance(bob, eq_bob)
    call check(eq_bank == -100000 .and. eq_alice == 74975 .and. eq_bob == 25025, 'balances')
    call check(debit == 100000 .and. credit == 25025, 'debit and credit')
    call check(eq_bank + eq_alice + eq_bob == 0, 'the books balance')
    call viewer%check()
    call check(.not. ctx%is_poisoned(), 'not poisoned')
    ! attestation and revocation
    call poster%set_attestation(TRUTH_BOTH)
    call books%post(alice, bob, 1_int64, 'NGN', 'PAY-0006', stat=st)
    call check(st == ZXV_E_CAP, 'a contradictory attestation blocks')
    call poster%set_attestation(TRUTH_TRUE)
    call books%post(alice, bob, 1_int64, 'NGN', 'PAY-0006', stat=st)
    call check(st == ZXV_OK, 'attested again')
    call root%revoke()
    call check(root%is_revoked() .and. .not. poster%check(CAP_LEDGER_POST) .and. .not. reader%check(CAP_LEDGER_READ), &
      'revocation reaches every descendant')
    call books%post(alice, bob, 1_int64, 'NGN', 'PAY-0007', stat=st)
    call check(st == ZXV_E_CAP, 'revoked')
  end subroutine

  ! R6: release in any order; the C handles still go children first.
  subroutine test_lifetimes()
    type(context) :: ctx, ctx2
    type(capability) :: root, child, copy
    type(ledger) :: led
    type(quarantine_log) :: log
    integer(int32) :: acct
    integer(int8) :: subject(32)
    call ctx%create()
    call ctx%root_capability(root)
    call root%attenuate(ior(CAP_LEDGER_POST, ior(CAP_LEDGER_READ, CAP_ATTEST)), child)
    call log%create(child, 4)
    call led%init(child)
    ! drop the context and the root first: the capability keeps them alive
    call ctx%destroy()
    call root%destroy()
    call check(.not. ctx%is_live() .and. .not. root%is_live() .and. child%is_live(), 'references dropped')
    call led%enable_currency('KES')
    call led%open_account('a', 'KES', ACCOUNT_HOLDER, acct)
    call check(acct >= 0, 'the ledger still works after its context variable was destroyed')
    subject = 0_int8
    call log%append(child, hexagram(0, HEX_POL_FORBIDDEN, 0), 1, 1_int64, subject)
    call check(log%count() == 1, 'the log still works')
    ! assignment shares the handle
    copy = child
    call child%destroy()
    call check(copy%check(CAP_LEDGER_READ), 'a copy keeps the capability')
    ctx2 = ctx
    call check(.not. ctx2%is_live(), 'copying a destroyed context copies nothing')
    call ctx2%create()
    call ctx2%create()
    call check(ctx2%is_live(), 'create twice replaces the handle')
    ! log, led, copy and ctx2 are finalized here; ASan checks the order in CI
  end subroutine

end program test_zxv
