! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! zxv.f90 - the ZXV SDK for Fortran 2008 and later: ISO_C_BINDING over the
! libzxv C ABI 1.3 (sdk/libzxv/include/zxv_api.h, sdk/docs/ABI.md).
!
!   type(rational)        exact int64/int64 rationals with + - * / ** and
!                         comparisons; no generic accepts a REAL, so a REAL
!                         argument is a compile-time error. The one way in
!                         from a REAL(REAL64) is rational_from_ieee754, which
!                         returns the double's exact value (0.1 becomes
!                         3602879701896397/36028797018963968).
!   type(truth)           four-valued logic: NEITHER, TRUE, FALSE, BOTH, with
!                         .and. .or. .not.; no conversion to LOGICAL except
!                         through an explicit policy (to_logical).
!   type(hexagram)        the 64-state tag (ABI 1.3) and its rules.
!   type(context)         a platform context (the ledger lives in it).
!   type(capability)      what a part of the program may do (R10).
!   type(ledger)          accounts, postings, balances through a capability.
!   type(quarantine_log)  the hash-chained log of forbidden work.
!
! Errors (sdk/docs/SDK_DESIGN.md section 5, in Fortran terms):
!   * Every call that can fail takes an optional INTEGER(INT32) stat. Given
!     stat, a USAGE, REFUSED or SYSTEM status is returned in it (ZXV_OK on
!     success) and the program goes on; the detail is last_error(). Without
!     stat, any failure stops the program with ERROR STOP 1 and a message,
!     the same rule as Fortran's own iostat=.
!   * Operators (+, -, *, /, **, comparisons) have no stat argument: an
!     exact result that does not fit int64/int64 is ERROR STOP 1. Use the
!     subroutine forms (rational_add, ...) with stat to handle it.
!   * FATAL (ZXV_E_POISONED, ZXV_E_INVARIANT) always stops the program
!     with ERROR STOP 70, stat or not. Why: Fortran has no exceptions, so
!     nothing would stop a caller from ignoring a stat value and carrying
!     on with a ledger that broke an invariant. ERROR STOP is the language's
!     only terminal form (it also stops every coarray image); no code after
!     it runs, and libzxv's poisoning means a restarted process cannot
!     reuse the state either. 70 is EX_SOFTWARE, as the Go SDK uses.
!
! Handles (R1, R6): context, capability and quarantine_log hold a counted
! reference to the C handle. A child keeps its parent alive (a capability
! its context, an attenuated capability its parent, a quarantine log its
! capability), so the C handles are always destroyed children first,
! whatever order Fortran finalizes or you call destroy() in. Assignment
! copies the reference. Variables of the main program are never finalized
! (they are implicitly SAVEd); call destroy() if that matters.
!
! Honest limits: capabilities are least-privilege discipline inside one
! process, not a sandbox (ABI.md R10). Rationals are bounded by int64:
! denominators grow and are then refused, never rounded.
module zxv
  use, intrinsic :: iso_c_binding
  use, intrinsic :: iso_fortran_env, only: int8, int32, int64, error_unit
  implicit none
  private

  ! ===== constants =====
  integer(int32), parameter, public :: ZXV_OK = 0, ZXV_E_ARG = -1, ZXV_E_BUFFER = -2, &
    ZXV_E_NOTIMPL = -3, ZXV_E_NOT_FOUND = -4, ZXV_E_CAPACITY = -5, ZXV_E_FUNDS = -6, &
    ZXV_E_CURRENCY = -7, ZXV_E_RANGE = -8, ZXV_E_STATE = -9, ZXV_E_INVARIANT = -10, &
    ZXV_E_UTF8 = -11, ZXV_E_TOO_LONG = -12, ZXV_E_DUPLICATE = -13, ZXV_E_NOMEM = -14, &
    ZXV_E_DECLINED = -15, ZXV_E_FIELD = -16, ZXV_E_CAP = -17, ZXV_E_POISONED = -18, &
    ZXV_E_BUDGET = -19, ZXV_E_RMAG_INVALID = -20, ZXV_E_VERIFY = -21, ZXV_E_FORMAT = -22, &
    ZXV_E_IO = -23, ZXV_E_FLOAT = -24, ZXV_E_INTERNAL = -99
  integer(int32), parameter, public :: ZXV_CLASS_OK = 0, ZXV_CLASS_USAGE = 1, &
    ZXV_CLASS_REFUSED = 2, ZXV_CLASS_FATAL = 3, ZXV_CLASS_SYSTEM = 4
  !> The exit code of ERROR STOP on a FATAL status.
  integer, parameter, public :: ZXV_FATAL_EXIT = 70

  integer(int64), parameter, public :: CAP_LEDGER_READ = 1_int64, CAP_LEDGER_POST = 2_int64, &
    CAP_CRYPTO_SIGN = 4_int64, CAP_CRYPTO_KEM = 8_int64, CAP_AI_INFERENCE = 16_int64, &
    CAP_BUDGET = 32_int64, CAP_PHASE = 64_int64, CAP_ATTEST = 128_int64, CAP_SWARM = 256_int64, &
    CAP_EVM = 512_int64, CAP_ALL = 1023_int64

  integer(int32), parameter, public :: ACCOUNT_HOLDER = 0, ACCOUNT_ISSUER = 1
  integer(int64), parameter, public :: ZXV_AMOUNT_MAX = 9007199254740991_int64

  integer(int32), parameter, public :: HEX_BASE_FALSE = 0, HEX_BASE_NEITHER = 1, &
    HEX_BASE_BOTH = 2, HEX_BASE_TRUE = 3
  integer(int32), parameter, public :: HEX_POL_ENTROPIC = 0, HEX_POL_NEUTRAL = 1, &
    HEX_POL_FORBIDDEN = 2, HEX_POL_NEGENTROPIC = 3
  integer(int32), parameter, public :: HEX_PHASE_POTENTIAL = 0, HEX_PHASE_EMERGING = 1, &
    HEX_PHASE_DISSOLVING = 2, HEX_PHASE_MANIFEST = 3
  integer(int32), parameter, public :: HEX_ACT_PROCEED = 0, HEX_ACT_WAIT = 1, &
    HEX_ACT_REFUSE = 2, HEX_ACT_ESCALATE = 3, HEX_ACT_ROLLBACK = 4
  integer(int32), parameter, public :: ZXV_FEAT_RATIONAL = int(z'00002000', int32), &
    ZXV_FEAT_LPRES = int(z'00004000', int32), ZXV_FEAT_CAPS = int(z'00008000', int32), &
    ZXV_FEAT_HEX = int(z'08000000', int32), ZXV_FEAT_LEDGER = 1

  ! ===== value types =====

  !> An exact rational: numerator / denominator, reduced, denominator > 0.
  type, public :: rational
    private
    integer(int64) :: n = 0_int64
    integer(int64) :: d = 1_int64
  contains
    procedure :: numerator => rat_numerator
    procedure :: denominator => rat_denominator
    procedure :: to_string => rat_to_string
    procedure :: to_q16 => rat_to_q16
    procedure, private :: rat_write
    generic :: write(formatted) => rat_write
  end type

  !> Four-valued truth (kernel numbering): NEITHER 0, TRUE 1, FALSE 2, BOTH 3.
  type, public :: truth
    private
    integer(int32) :: v = 0
  contains
    procedure :: value => truth_value
    procedure :: name => truth_name
    procedure :: is_certainly_true => truth_is_true
    procedure :: is_certainly_false => truth_is_false
    procedure :: is_both => truth_is_both
    procedure :: is_neither => truth_is_neither
    procedure :: to_logical => truth_to_logical
  end type

  type(truth), parameter, public :: TRUTH_NEITHER = truth(0), TRUTH_TRUE = truth(1), &
    TRUTH_FALSE = truth(2), TRUTH_BOTH = truth(3)

  !> The 64-state hexagram tag: base << 4 | polarity << 2 | phase.
  type, public :: hexagram
    private
    integer(int32) :: t = 20   ! neither / neutral / potential
  contains
    procedure :: tag => hex_tag
    procedure :: base => hex_base
    procedure :: polarity => hex_polarity_of
    procedure :: phase => hex_phase
    procedure :: is_forbidden => hex_is_forbidden
    procedure :: action => hex_action
    procedure :: action_name => hex_action_name
    procedure :: name => hex_name
    procedure :: step => hex_step
    procedure :: advance => hex_advance
    procedure :: with_state => hex_with_state
    procedure :: forbid => hex_forbid
    procedure :: to_truth => hex_to_truth
  end type

  ! ===== handles =====

  integer, parameter :: K_CTX = 1, K_CAP = 2, K_LOG = 3

  type :: zxv_node
    type(c_ptr) :: h = c_null_ptr
    integer :: kind = 0
    integer :: refs = 0
    type(zxv_node), pointer :: parent => null()
  end type

  !> A platform context: one ledger, its currencies and accounts.
  type, public :: context
    private
    type(zxv_node), pointer :: node => null()
  contains
    procedure :: create => ctx_create
    procedure :: destroy => ctx_destroy
    procedure :: is_live => ctx_is_live
    procedure :: is_poisoned => ctx_is_poisoned
    procedure :: halt => ctx_halt
    procedure :: root_capability => ctx_root_capability
    procedure, private :: ctx_assign
    generic :: assignment(=) => ctx_assign
    final :: ctx_final
  end type

  !> A capability (R10): a set of CAP_* bits, narrowed with attenuate.
  type, public :: capability
    private
    type(zxv_node), pointer :: node => null()
  contains
    procedure :: attenuate => cap_attenuate
    procedure :: revoke => cap_revoke
    procedure :: check => cap_check
    procedure :: bits => cap_bits
    procedure :: is_revoked => cap_is_revoked
    procedure :: set_attestation => cap_set_attestation
    procedure :: destroy => cap_destroy
    procedure :: is_live => cap_is_live
    procedure, private :: cap_assign
    generic :: assignment(=) => cap_assign
    final :: cap_final
  end type

  !> The ledger, reached through a capability (LEDGER_POST, LEDGER_READ).
  type, public :: ledger
    private
    type(capability) :: cap
  contains
    procedure :: init => led_init
    procedure :: enable_currency => led_enable_currency
    procedure :: open_account => led_open_account
    procedure :: post => led_post
    procedure :: balance => led_balance
    procedure :: check => led_check
  end type

  !> The quarantine log of forbidden work (ABI 1.3; needs CAP_ATTEST).
  type, public :: quarantine_log
    private
    type(zxv_node), pointer :: node => null()
  contains
    procedure :: create => log_create
    procedure :: append => log_append
    procedure :: count => log_count
    procedure :: head => log_head
    procedure :: record => log_record
    procedure :: verify => log_verify
    procedure :: destroy => log_destroy
    procedure, private :: log_assign
    generic :: assignment(=) => log_assign
    final :: log_final
  end type

  ! ===== generics =====

  !> rational(n) or rational(n, d) from INTEGER(INT32) or INTEGER(INT64).
  !> There is deliberately no specific for REAL: rational(0.5) does not compile.
  interface rational
    module procedure rat_from_i32, rat_from_i64, rat_from_i32_2, rat_from_i64_2
  end interface

  interface operator(+)
    module procedure rat_add_rr, rat_add_ri32, rat_add_ri64, rat_add_i32r, rat_add_i64r
  end interface
  interface operator(-)
    module procedure rat_sub_rr, rat_sub_ri32, rat_sub_ri64, rat_sub_i32r, rat_sub_i64r, rat_neg
  end interface
  interface operator(*)
    module procedure rat_mul_rr, rat_mul_ri32, rat_mul_ri64, rat_mul_i32r, rat_mul_i64r
  end interface
  interface operator(/)
    module procedure rat_div_rr, rat_div_ri32, rat_div_ri64, rat_div_i32r, rat_div_i64r
  end interface
  interface operator(==)
    module procedure rat_eq_rr, rat_eq_ri32, rat_eq_ri64, rat_eq_i32r, rat_eq_i64r
  end interface
  interface operator(/=)
    module procedure rat_ne_rr, rat_ne_ri32, rat_ne_ri64, rat_ne_i32r, rat_ne_i64r
  end interface
  interface operator(<)
    module procedure rat_lt_rr, rat_lt_ri32, rat_lt_ri64, rat_lt_i32r, rat_lt_i64r
  end interface
  interface operator(<=)
    module procedure rat_le_rr, rat_le_ri32, rat_le_ri64, rat_le_i32r, rat_le_i64r
  end interface
  interface operator(>)
    module procedure rat_gt_rr, rat_gt_ri32, rat_gt_ri64, rat_gt_i32r, rat_gt_i64r
  end interface
  interface operator(>=)
    module procedure rat_ge_rr, rat_ge_ri32, rat_ge_ri64, rat_ge_i32r, rat_ge_i64r
  end interface
  interface operator(**)
    module procedure rat_pow_i32, rat_pow_i64
  end interface
  interface assignment(=)
    module procedure rat_assign_i32, rat_assign_i64
  end interface
  interface abs
    module procedure rat_abs
  end interface

  interface operator(.and.)
    module procedure truth_and, hex_and
  end interface
  interface operator(.or.)
    module procedure truth_or, hex_or
  end interface
  interface operator(.not.)
    module procedure truth_not, hex_not
  end interface
  interface operator(==)
    module procedure truth_eq, hex_eq
  end interface
  interface operator(/=)
    module procedure truth_ne, hex_ne
  end interface

  !> to_string(x): exact text of a rational, or the name of a truth or hexagram.
  !> (Fortran cannot call a type-bound procedure on an expression such as (a + b).)
  interface to_string
    module procedure rat_to_string, truth_name, hex_name
  end interface
  interface numerator
    module procedure rat_numerator
  end interface
  interface denominator
    module procedure rat_denominator
  end interface

  !> hexagram(base, polarity, phase [, stat])
  interface hexagram
    module procedure hex_from_fields
  end interface


  public :: operator(+), operator(-), operator(*), operator(/), operator(**)
  public :: operator(==), operator(/=), operator(<), operator(<=), operator(>), operator(>=)
  public :: operator(.and.), operator(.or.), operator(.not.)
  public :: assignment(=), abs, to_string, numerator, denominator
  public :: rational_parse, rational_from_parts, rational_from_ieee754, rational_from_ieee754_bits
  public :: rational_add, rational_sub, rational_mul, rational_div
  public :: rational_sum, rational_dot, rational_split, rational_join
  public :: truth_of, truth_from_evidence
  public :: hexagram_from_tag, hexagram_from_truth, hex_phase_next, hex_polarity_from_evidence
  public :: zxv_abi_version, zxv_require_abi, zxv_feature_mask
  public :: status_name, status_class, last_error, bytes_to_hex

  ! ===== libzxv (R2: scalars, R3: status codes) =====
  interface
    integer(c_int32_t) function c_version(ma, mi, pa) bind(c, name='zxv_version')
      import :: c_int32_t
      integer(c_int32_t), intent(out) :: ma, mi, pa
    end function
    integer(c_int32_t) function c_features(mask) bind(c, name='zxv_features')
      import :: c_int32_t
      integer(c_int32_t), intent(out) :: mask
    end function
    type(c_ptr) function c_status_name(st) bind(c, name='zxv_status_name')
      import :: c_ptr, c_int32_t
      integer(c_int32_t), value :: st
    end function
    integer(c_int32_t) function c_status_class(st) bind(c, name='zxv_status_class')
      import :: c_int32_t
      integer(c_int32_t), value :: st
    end function
    integer(c_int32_t) function c_last_error(buf, cap, out_len) bind(c, name='zxv_last_error')
      import :: c_int32_t, c_ptr, c_size_t
      type(c_ptr), value :: buf
      integer(c_size_t), value :: cap
      integer(c_size_t), intent(out) :: out_len
    end function
    integer(c_size_t) function c_strlen(s) bind(c, name='strlen')
      import :: c_size_t, c_ptr
      type(c_ptr), value :: s
    end function

    integer(c_int32_t) function c_ctx_create(out) bind(c, name='zxv_ctx_create')
      import :: c_int32_t, c_ptr
      type(c_ptr), intent(out) :: out
    end function
    subroutine c_ctx_destroy(ctx) bind(c, name='zxv_ctx_destroy')
      import :: c_ptr
      type(c_ptr), value :: ctx
    end subroutine
    integer(c_int32_t) function c_ctx_is_poisoned(ctx, out) bind(c, name='zxv_ctx_is_poisoned')
      import :: c_int32_t, c_ptr
      type(c_ptr), value :: ctx
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_ctx_halt(ctx, reason) bind(c, name='zxv_ctx_halt')
      import :: c_int32_t, c_ptr, c_char
      type(c_ptr), value :: ctx
      character(kind=c_char), dimension(*), intent(in) :: reason
    end function

    integer(c_int32_t) function zxv_rat_normalize(n, d, on, od) bind(c, name='zxv_rat_normalize')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: n, d
      integer(c_int64_t), intent(out) :: on, od
    end function
    integer(c_int32_t) function zxv_rat_add(an, ad, bn, bd, on, od) bind(c, name='zxv_rat_add')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: an, ad, bn, bd
      integer(c_int64_t), intent(out) :: on, od
    end function
    integer(c_int32_t) function zxv_rat_sub(an, ad, bn, bd, on, od) bind(c, name='zxv_rat_sub')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: an, ad, bn, bd
      integer(c_int64_t), intent(out) :: on, od
    end function
    integer(c_int32_t) function zxv_rat_mul(an, ad, bn, bd, on, od) bind(c, name='zxv_rat_mul')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: an, ad, bn, bd
      integer(c_int64_t), intent(out) :: on, od
    end function
    integer(c_int32_t) function zxv_rat_div(an, ad, bn, bd, on, od) bind(c, name='zxv_rat_div')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: an, ad, bn, bd
      integer(c_int64_t), intent(out) :: on, od
    end function
    integer(c_int32_t) function zxv_rat_cmp(an, ad, bn, bd, out) bind(c, name='zxv_rat_cmp')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: an, ad, bn, bd
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_rat_parse(text, on, od) bind(c, name='zxv_rat_parse')
      import :: c_int32_t, c_int64_t, c_char
      character(kind=c_char), dimension(*), intent(in) :: text
      integer(c_int64_t), intent(out) :: on, od
    end function
    integer(c_int32_t) function c_rat_format(n, d, buf, cap, out_len) bind(c, name='zxv_rat_format')
      import :: c_int32_t, c_int64_t, c_ptr, c_size_t
      integer(c_int64_t), value :: n, d
      type(c_ptr), value :: buf
      integer(c_size_t), value :: cap
      integer(c_size_t), intent(out) :: out_len
    end function
    integer(c_int32_t) function c_rat_from_ieee754(bits, on, od) bind(c, name='zxv_rat_from_ieee754')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: bits
      integer(c_int64_t), intent(out) :: on, od
    end function
    integer(c_int32_t) function c_rat_to_q16(n, d, out) bind(c, name='zxv_rat_to_q16')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), value :: n, d
      integer(c_int32_t), intent(out) :: out
    end function

    integer(c_int32_t) function c_lp_not(a, out) bind(c, name='zxv_lp_not')
      import :: c_int32_t
      integer(c_int32_t), value :: a
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_lp_and(a, b, out) bind(c, name='zxv_lp_and')
      import :: c_int32_t
      integer(c_int32_t), value :: a, b
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_lp_or(a, b, out) bind(c, name='zxv_lp_or')
      import :: c_int32_t
      integer(c_int32_t), value :: a, b
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_lp_from_evidence(pos, neg, out) bind(c, name='zxv_lp_from_evidence')
      import :: c_int32_t
      integer(c_int32_t), value :: pos, neg
      integer(c_int32_t), intent(out) :: out
    end function
    type(c_ptr) function c_lp_name(v) bind(c, name='zxv_lp_name')
      import :: c_ptr, c_int32_t
      integer(c_int32_t), value :: v
    end function

    integer(c_int32_t) function c_cap_create(ctx, pid, identity, caps, out) bind(c, name='zxv_cap_create')
      import :: c_int32_t, c_int64_t, c_ptr
      type(c_ptr), value :: ctx, identity
      integer(c_int32_t), value :: pid
      integer(c_int64_t), value :: caps
      type(c_ptr), intent(out) :: out
    end function
    integer(c_int32_t) function c_cap_attenuate(parent, keep, out) bind(c, name='zxv_cap_attenuate')
      import :: c_int32_t, c_int64_t, c_ptr
      type(c_ptr), value :: parent
      integer(c_int64_t), value :: keep
      type(c_ptr), intent(out) :: out
    end function
    integer(c_int32_t) function c_cap_revoke(cap) bind(c, name='zxv_cap_revoke')
      import :: c_int32_t, c_ptr
      type(c_ptr), value :: cap
    end function
    integer(c_int32_t) function c_cap_set_attestation(cap, lp) bind(c, name='zxv_cap_set_attestation')
      import :: c_int32_t, c_ptr
      type(c_ptr), value :: cap
      integer(c_int32_t), value :: lp
    end function
    integer(c_int32_t) function c_cap_info(cap, caps, pid, att, revoked) bind(c, name='zxv_cap_info')
      import :: c_int32_t, c_int64_t, c_ptr
      type(c_ptr), value :: cap
      integer(c_int64_t), intent(out) :: caps
      integer(c_int32_t), intent(out) :: pid, att, revoked
    end function
    integer(c_int32_t) function c_cap_check(cap, needed) bind(c, name='zxv_cap_check')
      import :: c_int32_t, c_int64_t, c_ptr
      type(c_ptr), value :: cap
      integer(c_int64_t), value :: needed
    end function
    subroutine c_cap_destroy(cap) bind(c, name='zxv_cap_destroy')
      import :: c_ptr
      type(c_ptr), value :: cap
    end subroutine

    integer(c_int32_t) function c_ccy_enable_cap(cap, alpha) bind(c, name='zxv_ccy_enable_cap')
      import :: c_int32_t, c_ptr, c_char
      type(c_ptr), value :: cap
      character(kind=c_char), dimension(*), intent(in) :: alpha
    end function
    integer(c_int32_t) function c_ccy_enable_with_minor_cap(cap, alpha, minor) &
        bind(c, name='zxv_ccy_enable_with_minor_cap')
      import :: c_int32_t, c_int8_t, c_ptr, c_char
      type(c_ptr), value :: cap
      character(kind=c_char), dimension(*), intent(in) :: alpha
      integer(c_int8_t), value :: minor
    end function
    integer(c_int32_t) function c_open_account_cap(cap, name, ccy, kind, out) &
        bind(c, name='zxv_ledger_open_account_cap')
      import :: c_int32_t, c_ptr, c_char
      type(c_ptr), value :: cap
      character(kind=c_char), dimension(*), intent(in) :: name, ccy
      integer(c_int32_t), value :: kind
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_post_cap(cap, from, to, amount, ccy, ref, out) bind(c, name='zxv_ledger_post_cap')
      import :: c_int32_t, c_int64_t, c_ptr, c_char
      type(c_ptr), value :: cap
      integer(c_int32_t), value :: from, to
      integer(c_int64_t), value :: amount
      character(kind=c_char), dimension(*), intent(in) :: ccy, ref
      integer(c_int64_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_balance_cap(cap, acct, debit, credit, equity) bind(c, name='zxv_ledger_balance_cap')
      import :: c_int32_t, c_int64_t, c_ptr
      type(c_ptr), value :: cap
      integer(c_int32_t), value :: acct
      integer(c_int64_t), intent(out) :: debit, credit, equity
    end function
    integer(c_int32_t) function c_check_cap(cap) bind(c, name='zxv_ledger_check_cap')
      import :: c_int32_t, c_ptr
      type(c_ptr), value :: cap
    end function

    integer(c_int32_t) function c_hex_pack(b, p, ph, out) bind(c, name='zxv_hex_pack')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: b, p, ph
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_unpack(t, b, p, ph) bind(c, name='zxv_hex_unpack')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: t
      integer(c_int8_t), intent(out) :: b, p, ph
    end function
    integer(c_int32_t) function c_hex_action(t, out) bind(c, name='zxv_hex_action')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: t
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_phase_next(ph, out) bind(c, name='zxv_hex_phase_next')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: ph
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_step(t, ph, out) bind(c, name='zxv_hex_step')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: t, ph
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_set(t, b, p, out) bind(c, name='zxv_hex_set')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: t, b, p
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_forbid(t, out) bind(c, name='zxv_hex_forbid')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: t
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_and(a, b, out) bind(c, name='zxv_hex_and')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: a, b
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_or(a, b, out) bind(c, name='zxv_hex_or')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: a, b
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_not(a, out) bind(c, name='zxv_hex_not')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: a
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_polarity(p0, n0, p1, n1, stale, out) bind(c, name='zxv_hex_polarity')
      import :: c_int32_t, c_int8_t
      integer(c_int32_t), value :: p0, n0, p1, n1, stale
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_from_lp(lp, out) bind(c, name='zxv_hex_from_lp')
      import :: c_int32_t, c_int8_t
      integer(c_int32_t), value :: lp
      integer(c_int8_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hex_to_lp(b, out) bind(c, name='zxv_hex_to_lp')
      import :: c_int32_t, c_int8_t
      integer(c_int8_t), value :: b
      integer(c_int32_t), intent(out) :: out
    end function
    type(c_ptr) function c_hex_name(t) bind(c, name='zxv_hex_name')
      import :: c_ptr, c_int8_t
      integer(c_int8_t), value :: t
    end function
    type(c_ptr) function c_hex_action_name(a) bind(c, name='zxv_hex_action_name')
      import :: c_ptr, c_int32_t
      integer(c_int32_t), value :: a
    end function

    integer(c_int32_t) function c_hexlog_create(cap, capacity, out) bind(c, name='zxv_hexlog_create')
      import :: c_int32_t, c_ptr
      type(c_ptr), value :: cap
      integer(c_int32_t), value :: capacity
      type(c_ptr), intent(out) :: out
    end function
    subroutine c_hexlog_destroy(log) bind(c, name='zxv_hexlog_destroy')
      import :: c_ptr
      type(c_ptr), value :: log
    end subroutine
    integer(c_int32_t) function c_hexlog_append(cap, log, tag, reason, submitter, subject, seq) &
        bind(c, name='zxv_hexlog_append')
      import :: c_int32_t, c_int64_t, c_int8_t, c_ptr
      type(c_ptr), value :: cap, log
      integer(c_int8_t), value :: tag, reason
      integer(c_int32_t), value :: submitter
      integer(c_int8_t), dimension(32), intent(in) :: subject
      integer(c_int64_t), intent(out) :: seq
    end function
    integer(c_int32_t) function c_hexlog_count(log, out) bind(c, name='zxv_hexlog_count')
      import :: c_int32_t, c_ptr
      type(c_ptr), value :: log
      integer(c_int32_t), intent(out) :: out
    end function
    integer(c_int32_t) function c_hexlog_head(log, out) bind(c, name='zxv_hexlog_head')
      import :: c_int32_t, c_int8_t, c_ptr
      type(c_ptr), value :: log
      integer(c_int8_t), dimension(32), intent(out) :: out
    end function
    integer(c_int32_t) function c_hexlog_at(log, idx, seq, tag, reason, submitter, subject, prev, hash) &
        bind(c, name='zxv_hexlog_at')
      import :: c_int32_t, c_int64_t, c_int8_t, c_ptr
      type(c_ptr), value :: log
      integer(c_int32_t), value :: idx
      integer(c_int64_t), intent(out) :: seq
      integer(c_int8_t), intent(out) :: tag, reason
      integer(c_int32_t), intent(out) :: submitter
      integer(c_int8_t), dimension(32), intent(out) :: subject, prev, hash
    end function
    integer(c_int32_t) function c_hexlog_verify(log) bind(c, name='zxv_hexlog_verify')
      import :: c_int32_t, c_ptr
      type(c_ptr), value :: log
    end function
  end interface

  logical, save :: abi_checked = .false.

contains

  ! ===================================================================
  ! status handling
  ! ===================================================================

  !> The name of a status code ("ZXV_E_RANGE").
  function status_name(st) result(s)
    integer(int32), intent(in) :: st
    character(len=:), allocatable :: s
    s = from_cstr(c_status_name(st))
  end function

  !> ZXV_CLASS_OK, _USAGE, _REFUSED, _FATAL or _SYSTEM.
  integer(int32) function status_class(st)
    integer(int32), intent(in) :: st
    status_class = c_status_class(st)
  end function

  !> The calling thread's last error detail (R7). Branch on status codes, not on this text.
  function last_error() result(s)
    character(len=:), allocatable :: s
    character(kind=c_char), allocatable, target :: buf(:)
    integer(c_size_t) :: need, room
    integer(c_int32_t) :: st
    integer :: i
    need = 0
    st = c_last_error(c_null_ptr, 0_c_size_t, need)
    room = need + 1
    allocate(buf(room))
    st = c_last_error(c_loc(buf), room, need)
    allocate(character(len=int(need)) :: s)
    do i = 1, int(need)
      s(i:i) = buf(i)
    end do
  end function

  !> Report a status: stat given, return it; FATAL always stops (ERROR STOP 70);
  !> any other failure without stat stops with ERROR STOP 1.
  subroutine handle(st, what, stat)
    integer(c_int32_t), intent(in) :: st
    character(len=*), intent(in) :: what
    integer(int32), intent(out), optional :: stat
    if (present(stat)) stat = st
    if (st == ZXV_OK) return
    if (c_status_class(st) == ZXV_CLASS_FATAL) call fatal_stop(st, what)
    if (.not. present(stat)) then
      write (error_unit, '(a)') 'zxv: ' // what // ' failed: ' // status_name(st) // ': ' // last_error() // &
        ' (pass stat= to handle it)'
      flush (error_unit)
      error stop 1
    end if
  end subroutine

  subroutine fatal_stop(st, what)
    integer(c_int32_t), intent(in) :: st
    character(len=*), intent(in) :: what
    write (error_unit, '(a)') 'zxv: FATAL ' // status_name(st) // ' in ' // what // ': ' // last_error()
    write (error_unit, '(a)') 'zxv: the context is poisoned: no call on it can succeed, so the program stops here'
    flush (error_unit)
    error stop ZXV_FATAL_EXIT
  end subroutine

  ! ===================================================================
  ! library
  ! ===================================================================

  !> The libzxv ABI version.
  subroutine zxv_abi_version(major, minor, patch)
    integer(int32), intent(out) :: major, minor, patch
    integer(c_int32_t) :: st
    st = c_version(major, minor, patch)
  end subroutine

  !> Refuse a libzxv whose major is not 1 or whose minor is below 3 (SDK_DESIGN.md 1).
  subroutine zxv_require_abi(stat)
    integer(int32), intent(out), optional :: stat
    integer(int32) :: ma, mi, pa
    call zxv_abi_version(ma, mi, pa)
    if (ma /= 1 .or. mi < 3) then
      if (present(stat)) then
        stat = ZXV_E_NOTIMPL
        return
      end if
      write (error_unit, '(a,i0,a,i0,a)') 'zxv: libzxv ', ma, '.', mi, ' found; this module needs 1.3 or a later 1.x'
      error stop 1
    end if
    abi_checked = .true.
    if (present(stat)) stat = ZXV_OK
  end subroutine

  !> The ZXV_FEAT_* groups live in this build (R9).
  integer(int32) function zxv_feature_mask()
    integer(c_int32_t) :: st
    st = c_features(zxv_feature_mask)
  end function

  ! ===================================================================
  ! helpers
  ! ===================================================================

  function from_cstr(p) result(s)
    type(c_ptr), intent(in) :: p
    character(len=:), allocatable :: s
    character(kind=c_char), pointer :: a(:)
    integer :: n, i
    if (.not. c_associated(p)) then
      s = ''
      return
    end if
    n = int(c_strlen(p))
    call c_f_pointer(p, a, [n])
    allocate(character(len=n) :: s)
    do i = 1, n
      s(i:i) = a(i)
    end do
  end function

  function cstr(s) result(c)
    character(len=*), intent(in) :: s
    character(kind=c_char, len=:), allocatable :: c
    c = trim(s) // c_null_char
  end function

  !> An unsigned byte 0..255 as C's uint8_t.
  integer(c_int8_t) function u8(i)
    integer(int32), intent(in) :: i
    if (i > 127) then
      u8 = int(i - 256, c_int8_t)
    else
      u8 = int(i, c_int8_t)
    end if
  end function

  integer(int32) function from_u8(b)
    integer(c_int8_t), intent(in) :: b
    from_u8 = iand(int(b, int32), 255)
  end function

  !> An unsigned 32-bit value held in an int64, as C's uint32_t.
  integer(c_int32_t) function u32(v)
    integer(int64), intent(in) :: v
    if (v > 2147483647_int64) then
      u32 = int(v - 4294967296_int64, c_int32_t)
    else
      u32 = int(v, c_int32_t)
    end if
  end function

  integer(int64) function from_u32(v)
    integer(c_int32_t), intent(in) :: v
    from_u32 = iand(int(v, int64), 4294967295_int64)
  end function

  !> Lower-case hex of a byte array (int8 values read as 0..255).
  function bytes_to_hex(b) result(s)
    integer(int8), intent(in) :: b(:)
    character(len=2 * size(b)) :: s
    character(len=16), parameter :: hx = '0123456789abcdef'
    integer :: i, v
    do i = 1, size(b)
      v = iand(int(b(i)), 255)
      s(2 * i - 1:2 * i - 1) = hx(v / 16 + 1:v / 16 + 1)
      s(2 * i:2 * i) = hx(mod(v, 16) + 1:mod(v, 16) + 1)
    end do
  end function

  ! ===================================================================
  ! handle nodes (R6: children before parents)
  ! ===================================================================

  function node_new(h, kind, parent) result(p)
    type(c_ptr), intent(in) :: h
    integer, intent(in) :: kind
    type(zxv_node), pointer, intent(in) :: parent
    type(zxv_node), pointer :: p
    allocate(p)
    p%h = h
    p%kind = kind
    p%refs = 1
    p%parent => parent
    if (associated(parent)) parent%refs = parent%refs + 1
  end function

  recursive subroutine node_release(p)
    type(zxv_node), pointer, intent(inout) :: p
    type(zxv_node), pointer :: par
    if (.not. associated(p)) return
    p%refs = p%refs - 1
    if (p%refs > 0) then
      p => null()
      return
    end if
    select case (p%kind)
    case (K_CTX)
      call c_ctx_destroy(p%h)
    case (K_CAP)
      call c_cap_destroy(p%h)
    case (K_LOG)
      call c_hexlog_destroy(p%h)
    end select
    par => p%parent
    deallocate(p)
    call node_release(par)
  end subroutine

  subroutine node_share(dst, src)
    type(zxv_node), pointer, intent(inout) :: dst
    type(zxv_node), pointer, intent(in) :: src
    type(zxv_node), pointer :: keep
    keep => src
    if (associated(keep)) keep%refs = keep%refs + 1
    call node_release(dst)
    dst => keep
  end subroutine

  function node_handle(p, what) result(h)
    type(zxv_node), pointer, intent(in) :: p
    character(len=*), intent(in) :: what
    type(c_ptr) :: h
    if (.not. associated(p)) then
      write (error_unit, '(a)') 'zxv: ' // what // ': the handle was never created or was destroyed'
      error stop 1
    end if
    h = p%h
  end function

  ! ===================================================================
  ! context
  ! ===================================================================

  subroutine ctx_create(self, stat)
    class(context), intent(inout) :: self
    integer(int32), intent(out), optional :: stat
    type(c_ptr) :: h
    type(zxv_node), pointer :: none
    integer(c_int32_t) :: st
    if (.not. abi_checked) call zxv_require_abi()
    call node_release(self%node)
    st = c_ctx_create(h)
    call handle(st, 'context%create', stat)
    if (st /= ZXV_OK) return
    none => null()
    self%node => node_new(h, K_CTX, none)
  end subroutine

  !> Drop this reference. The C context is destroyed once nothing made from it is left.
  subroutine ctx_destroy(self)
    class(context), intent(inout) :: self
    call node_release(self%node)
  end subroutine

  logical function ctx_is_live(self)
    class(context), intent(in) :: self
    ctx_is_live = associated(self%node)
  end function

  !> .true. once the context halted (it never comes back).
  logical function ctx_is_poisoned(self)
    class(context), intent(in) :: self
    integer(c_int32_t) :: st, p
    st = c_ctx_is_poisoned(node_handle(self%node, 'context%is_poisoned'), p)
    ctx_is_poisoned = st == ZXV_OK .and. p /= 0
  end function

  !> Halt the context on purpose (an operator kill switch; tests). Every later call
  !> on it, or on anything made from it, is FATAL and stops the program.
  subroutine ctx_halt(self, reason)
    class(context), intent(inout) :: self
    character(len=*), intent(in) :: reason
    integer(c_int32_t) :: st
    st = c_ctx_halt(node_handle(self%node, 'context%halt'), cstr(reason))
  end subroutine

  !> A root capability holding `caps` (default CAP_ALL) for this context.
  subroutine ctx_root_capability(self, cap, caps, process_id, stat)
    class(context), intent(in) :: self
    type(capability), intent(inout) :: cap
    integer(int64), intent(in), optional :: caps
    integer(int32), intent(in), optional :: process_id
    integer(int32), intent(out), optional :: stat
    integer(int64) :: bits
    integer(int32) :: pid
    integer(c_int32_t) :: st
    type(c_ptr) :: h
    bits = CAP_ALL
    pid = 1
    if (present(caps)) bits = caps
    if (present(process_id)) pid = process_id
    st = c_cap_create(node_handle(self%node, 'context%root_capability'), pid, c_null_ptr, bits, h)
    call handle(st, 'context%root_capability', stat)
    if (st /= ZXV_OK) return
    call node_release(cap%node)
    cap%node => node_new(h, K_CAP, self%node)
  end subroutine

  subroutine ctx_assign(lhs, rhs)
    class(context), intent(inout) :: lhs
    class(context), intent(in) :: rhs
    call node_share(lhs%node, rhs%node)
  end subroutine

  impure elemental subroutine ctx_final(self)
    type(context), intent(inout) :: self
    call node_release(self%node)
  end subroutine

  ! ===================================================================
  ! capability
  ! ===================================================================

  !> A child holding (these bits .and. keep). Revoking this one revokes the child.
  subroutine cap_attenuate(self, keep, child, stat)
    class(capability), intent(in) :: self
    integer(int64), intent(in) :: keep
    type(capability), intent(inout) :: child
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: st
    type(c_ptr) :: h
    type(zxv_node), pointer :: fresh
    st = c_cap_attenuate(node_handle(self%node, 'capability%attenuate'), keep, h)
    call handle(st, 'capability%attenuate', stat)
    if (st /= ZXV_OK) return
    fresh => node_new(h, K_CAP, self%node)
    call node_release(child%node)
    child%node => fresh
  end subroutine

  subroutine cap_revoke(self, stat)
    class(capability), intent(inout) :: self
    integer(int32), intent(out), optional :: stat
    call handle(c_cap_revoke(node_handle(self%node, 'capability%revoke')), 'capability%revoke', stat)
  end subroutine

  !> .true. if a call needing `needed` would pass this capability.
  logical function cap_check(self, needed)
    class(capability), intent(in) :: self
    integer(int64), intent(in) :: needed
    integer(c_int32_t) :: st
    st = c_cap_check(node_handle(self%node, 'capability%check'), needed)
    if (c_status_class(st) == ZXV_CLASS_FATAL) call fatal_stop(st, 'capability%check')
    cap_check = st == ZXV_OK
  end function

  integer(int64) function cap_bits(self)
    class(capability), intent(in) :: self
    integer(c_int32_t) :: st, pid, att, rev
    st = c_cap_info(node_handle(self%node, 'capability%bits'), cap_bits, pid, att, rev)
    call handle(st, 'capability%bits')
  end function

  logical function cap_is_revoked(self)
    class(capability), intent(in) :: self
    integer(c_int32_t) :: st, pid, att, rev
    integer(int64) :: bits
    st = c_cap_info(node_handle(self%node, 'capability%is_revoked'), bits, pid, att, rev)
    call handle(st, 'capability%is_revoked')
    cap_is_revoked = rev /= 0
  end function

  !> Anything but TRUTH_TRUE blocks every gated call made with this capability.
  subroutine cap_set_attestation(self, t, stat)
    class(capability), intent(inout) :: self
    type(truth), intent(in) :: t
    integer(int32), intent(out), optional :: stat
    call handle(c_cap_set_attestation(node_handle(self%node, 'capability%set_attestation'), t%v), &
      'capability%set_attestation', stat)
  end subroutine

  subroutine cap_destroy(self)
    class(capability), intent(inout) :: self
    call node_release(self%node)
  end subroutine

  logical function cap_is_live(self)
    class(capability), intent(in) :: self
    cap_is_live = associated(self%node)
  end function

  subroutine cap_assign(lhs, rhs)
    class(capability), intent(inout) :: lhs
    class(capability), intent(in) :: rhs
    call node_share(lhs%node, rhs%node)
  end subroutine

  impure elemental subroutine cap_final(self)
    type(capability), intent(inout) :: self
    call node_release(self%node)
  end subroutine

  ! ===================================================================
  ! ledger
  ! ===================================================================

  !> Work on the ledger of cap's context, with cap's rights.
  subroutine led_init(self, cap)
    class(ledger), intent(inout) :: self
    type(capability), intent(in) :: cap
    self%cap = cap
  end subroutine

  !> Enable an ISO 4217 currency (or set its minor units). Needs LEDGER_POST.
  subroutine led_enable_currency(self, alpha, minor_units, stat)
    class(ledger), intent(inout) :: self
    character(len=*), intent(in) :: alpha
    integer(int32), intent(in), optional :: minor_units
    integer(int32), intent(out), optional :: stat
    type(c_ptr) :: h
    h = node_handle(self%cap%node, 'ledger%enable_currency')
    if (present(minor_units)) then
      call handle(c_ccy_enable_with_minor_cap(h, cstr(alpha), u8(minor_units)), 'ledger%enable_currency', stat)
    else
      call handle(c_ccy_enable_cap(h, cstr(alpha)), 'ledger%enable_currency', stat)
    end if
  end subroutine

  !> Open an account (ACCOUNT_HOLDER may never go below zero; ACCOUNT_ISSUER may).
  subroutine led_open_account(self, name, ccy, kind, id, stat)
    class(ledger), intent(inout) :: self
    character(len=*), intent(in) :: name, ccy
    integer(int32), intent(in) :: kind
    integer(int32), intent(out) :: id
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: st, out
    st = c_open_account_cap(node_handle(self%cap%node, 'ledger%open_account'), cstr(name), cstr(ccy), kind, out)
    call handle(st, 'ledger%open_account', stat)
    if (st == ZXV_OK) id = out
  end subroutine

  !> Post amount_minor (> 0, exact minor units) from one account to another.
  subroutine led_post(self, from, to, amount_minor, ccy, reference, entry, stat)
    class(ledger), intent(inout) :: self
    integer(int32), intent(in) :: from, to
    integer(int64), intent(in) :: amount_minor
    character(len=*), intent(in) :: ccy, reference
    integer(int64), intent(out), optional :: entry
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: st
    integer(c_int64_t) :: e
    st = c_post_cap(node_handle(self%cap%node, 'ledger%post'), from, to, amount_minor, cstr(ccy), cstr(reference), e)
    call handle(st, 'ledger%post', stat)
    if (st == ZXV_OK .and. present(entry)) entry = e
  end subroutine

  !> Equity (debit - credit) of an account in minor units; debit and credit optional.
  subroutine led_balance(self, account, equity, debit, credit, stat)
    class(ledger), intent(inout) :: self
    integer(int32), intent(in) :: account
    integer(int64), intent(out) :: equity
    integer(int64), intent(out), optional :: debit, credit
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: st
    integer(c_int64_t) :: d, c, e
    st = c_balance_cap(node_handle(self%cap%node, 'ledger%balance'), account, d, c, e)
    call handle(st, 'ledger%balance', stat)
    if (st /= ZXV_OK) return
    equity = e
    if (present(debit)) debit = d
    if (present(credit)) credit = c
  end subroutine

  !> Verify every ledger invariant. A broken one poisons the context: FATAL.
  subroutine led_check(self, stat)
    class(ledger), intent(inout) :: self
    integer(int32), intent(out), optional :: stat
    call handle(c_check_cap(node_handle(self%cap%node, 'ledger%check')), 'ledger%check', stat)
  end subroutine

  ! ===================================================================
  ! quarantine log
  ! ===================================================================

  subroutine log_create(self, cap, capacity, stat)
    class(quarantine_log), intent(inout) :: self
    type(capability), intent(in) :: cap
    integer(int32), intent(in) :: capacity
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: st
    type(c_ptr) :: h
    st = c_hexlog_create(node_handle(cap%node, 'quarantine_log%create'), capacity, h)
    call handle(st, 'quarantine_log%create', stat)
    if (st /= ZXV_OK) return
    call node_release(self%node)
    self%node => node_new(h, K_LOG, cap%node)
  end subroutine

  !> Quarantine forbidden work. A full log is ZXV_E_CAPACITY: then halt the context.
  subroutine log_append(self, cap, tag, reason, submitter, subject, seq, stat)
    class(quarantine_log), intent(inout) :: self
    type(capability), intent(in) :: cap
    type(hexagram), intent(in) :: tag
    integer(int32), intent(in) :: reason
    integer(int64), intent(in) :: submitter
    integer(int8), intent(in) :: subject(32)
    integer(int64), intent(out), optional :: seq
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: st
    integer(c_int64_t) :: s
    st = c_hexlog_append(node_handle(cap%node, 'quarantine_log%append'), node_handle(self%node, 'quarantine_log%append'), &
      u8(tag%t), u8(reason), u32(submitter), subject, s)
    call handle(st, 'quarantine_log%append', stat)
    if (st == ZXV_OK .and. present(seq)) seq = s
  end subroutine

  integer(int32) function log_count(self)
    class(quarantine_log), intent(in) :: self
    integer(c_int32_t) :: n
    call handle(c_hexlog_count(node_handle(self%node, 'quarantine_log%count'), n), 'quarantine_log%count')
    log_count = n
  end function

  !> The chain head as 64 hex digits (zeros when empty).
  function log_head(self) result(s)
    class(quarantine_log), intent(in) :: self
    character(len=64) :: s
    integer(int8) :: h(32)
    call handle(c_hexlog_head(node_handle(self%node, 'quarantine_log%head'), h), 'quarantine_log%head')
    s = bytes_to_hex(h)
  end function

  !> Record `index` (0-based, oldest first), so the SHA-256 chain can be recomputed.
  subroutine log_record(self, index, seq, tag, reason, submitter, subject, prev, hash, stat)
    class(quarantine_log), intent(in) :: self
    integer(int32), intent(in) :: index
    integer(int64), intent(out) :: seq, submitter
    type(hexagram), intent(out) :: tag
    integer(int32), intent(out) :: reason
    integer(int8), intent(out) :: subject(32), prev(32), hash(32)
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: st, sub
    integer(c_int8_t) :: t, r
    integer(c_int64_t) :: sq
    st = c_hexlog_at(node_handle(self%node, 'quarantine_log%record'), index, sq, t, r, sub, subject, prev, hash)
    call handle(st, 'quarantine_log%record', stat)
    if (st /= ZXV_OK) return
    seq = sq
    tag%t = from_u8(t)
    reason = from_u8(r)
    submitter = from_u32(sub)
  end subroutine

  !> Recompute every link; a broken chain poisons the context (FATAL).
  subroutine log_verify(self, stat)
    class(quarantine_log), intent(in) :: self
    integer(int32), intent(out), optional :: stat
    call handle(c_hexlog_verify(node_handle(self%node, 'quarantine_log%verify')), 'quarantine_log%verify', stat)
  end subroutine

  subroutine log_destroy(self)
    class(quarantine_log), intent(inout) :: self
    call node_release(self%node)
  end subroutine

  subroutine log_assign(lhs, rhs)
    class(quarantine_log), intent(inout) :: lhs
    class(quarantine_log), intent(in) :: rhs
    call node_share(lhs%node, rhs%node)
  end subroutine

  impure elemental subroutine log_final(self)
    type(quarantine_log), intent(inout) :: self
    call node_release(self%node)
  end subroutine

  ! ===================================================================
  ! rational
  ! ===================================================================

  function rat_make(n, d, what) result(r)
    integer(int64), intent(in) :: n, d
    character(len=*), intent(in) :: what
    type(rational) :: r
    call handle(zxv_rat_normalize(n, d, r%n, r%d), what)
  end function

  function rat_from_i32(n) result(r)
    integer(int32), intent(in) :: n
    type(rational) :: r
    r%n = int(n, int64)
    r%d = 1_int64
  end function

  function rat_from_i64(n) result(r)
    integer(int64), intent(in) :: n
    type(rational) :: r
    r = rat_make(n, 1_int64, 'rational(n)')
  end function

  function rat_from_i32_2(n, d) result(r)
    integer(int32), intent(in) :: n, d
    type(rational) :: r
    r = rat_make(int(n, int64), int(d, int64), 'rational(n, d)')
  end function

  function rat_from_i64_2(n, d) result(r)
    integer(int64), intent(in) :: n, d
    type(rational) :: r
    r = rat_make(n, d, 'rational(n, d)')
  end function

  !> n/d with a stat: ZXV_E_ARG for d = 0, ZXV_E_RANGE for -2**63.
  function rational_from_parts(n, d, stat) result(r)
    integer(int64), intent(in) :: n, d
    integer(int32), intent(out), optional :: stat
    type(rational) :: r
    type(rational) :: t
    integer(c_int32_t) :: st
    st = zxv_rat_normalize(n, d, t%n, t%d)
    call handle(st, 'rational_from_parts', stat)
    if (st == ZXV_OK) r = t
  end function

  !> Exact text: "12.75", "-0.1", "3/4", "42". ZXV_E_FORMAT for "1e5", " 1", "NaN".
  function rational_parse(text, stat) result(r)
    character(len=*), intent(in) :: text
    integer(int32), intent(out), optional :: stat
    type(rational) :: r
    type(rational) :: t
    integer(c_int32_t) :: st
    st = c_rat_parse(text // c_null_char, t%n, t%d)
    call handle(st, 'rational_parse("' // text // '")', stat)
    if (st == ZXV_OK) r = t
  end function

  !> The exact value of the IEEE-754 binary64 with these raw bits.
  function rational_from_ieee754_bits(bits, stat) result(r)
    integer(int64), intent(in) :: bits
    integer(int32), intent(out), optional :: stat
    type(rational) :: r
    type(rational) :: t
    integer(c_int32_t) :: st
    st = c_rat_from_ieee754(bits, t%n, t%d)
    call handle(st, 'rational_from_ieee754', stat)
    if (st == ZXV_OK) r = t
  end function

  ! R11-BOUNDARY-BEGIN: the only place this module touches a REAL. It takes
  ! a REAL(REAL64) on purpose, by name, and returns the double's exact value
  ! (0.1d0 becomes 3602879701896397/36028797018963968, never 1/10). NaN and
  ! infinity are ZXV_E_FLOAT. It is a specific procedure, in no generic.
  function rational_from_ieee754(x, stat) result(r)
    use, intrinsic :: iso_fortran_env, only: real64
    real(real64), intent(in) :: x
    integer(int32), intent(out), optional :: stat
    type(rational) :: r
    r = rational_from_ieee754_bits(transfer(x, 0_int64), stat)
  end function
  ! R11-BOUNDARY-END

  integer(int64) function rat_numerator(self)
    class(rational), intent(in) :: self
    rat_numerator = self%n
  end function

  integer(int64) function rat_denominator(self)
    class(rational), intent(in) :: self
    rat_denominator = self%d
  end function

  !> Exact text: a terminating decimal when there is one ("12.75"), else "n/d".
  function rat_to_string(self) result(s)
    class(rational), intent(in) :: self
    character(len=:), allocatable :: s
    character(kind=c_char), allocatable, target :: buf(:)
    integer(c_size_t) :: need, room
    integer(c_int32_t) :: st
    integer :: i
    need = 0
    st = c_rat_format(self%n, self%d, c_null_ptr, 0_c_size_t, need)
    room = need + 1
    allocate(buf(room))
    st = c_rat_format(self%n, self%d, c_loc(buf), room, need)
    call handle(st, 'rational%to_string')
    allocate(character(len=int(need)) :: s)
    do i = 1, int(need)
      s(i:i) = buf(i)
    end do
  end function

  !> Round to Q16.16 (half away from zero), for engine parameters.
  integer(int32) function rat_to_q16(self, stat)
    class(rational), intent(in) :: self
    integer(int32), intent(out), optional :: stat
    integer(c_int32_t) :: q, st
    q = 0
    st = c_rat_to_q16(self%n, self%d, q)
    call handle(st, 'rational%to_q16', stat)
    rat_to_q16 = 0
    if (st == ZXV_OK) rat_to_q16 = q
  end function

  subroutine rat_write(dtv, unit, iotype, v_list, iostat, iomsg)
    class(rational), intent(in) :: dtv
    integer, intent(in) :: unit
    character(len=*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(len=*), intent(inout) :: iomsg
    if (.false.) iomsg = iotype // char(size(v_list))
    write (unit, '(a)', iostat=iostat) dtv%to_string()
  end subroutine

  function itoa(i) result(s)
    integer(int64), intent(in) :: i
    character(len=:), allocatable :: s
    character(len=24) :: b
    write (b, '(i0)') i
    s = trim(b)
  end function

  subroutine rat_refused(st, op, a, b)
    integer(c_int32_t), intent(in) :: st
    character(len=*), intent(in) :: op
    type(rational), intent(in) :: a, b
    if (st == ZXV_E_ARG .and. op == '/') then
      write (error_unit, '(a)') 'zxv: rational division by zero: ' // a%to_string() // ' / 0'
    else
      write (error_unit, '(a)') 'zxv: ' // a%to_string() // ' ' // op // ' ' // b%to_string() // &
        ' does not fit int64/int64 exactly (' // status_name(st) // '; refused, never rounded). ' // &
        'Use rational_' // merge('add', merge('sub', merge('mul', 'div', op == '*'), op == '-'), op == '+') // &
        '(a, b, r, stat) to handle it.'
    end if
    flush (error_unit)
    error stop 1
  end subroutine

  !> -1, 0 or +1 as an/ad <, = or > bn/bd, for ad, bd > 0 and any int64
  !> numerators. Compares the integer parts (floor), then the reciprocals
  !> of the fractional parts with the order flipped: Euclid's algorithm on
  !> both fractions at once, so every intermediate fits in int64.
  pure integer function rat_order(an0, ad0, bn0, bd0) result(c)
    integer(int64), intent(in) :: an0, ad0, bn0, bd0
    integer(int64) :: an, ad, bn, bd, qa, qb, ra, rb
    integer :: sgn
    an = an0; ad = ad0; bn = bn0; bd = bd0
    sgn = 1
    do
      ra = modulo(an, ad)
      rb = modulo(bn, bd)
      qa = an / ad
      if (an < 0 .and. ra /= 0) qa = qa - 1
      qb = bn / bd
      if (bn < 0 .and. rb /= 0) qb = qb - 1
      if (qa /= qb) then
        c = sgn
        if (qa < qb) c = -sgn
        return
      end if
      if (ra == 0 .and. rb == 0) then
        c = 0
        return
      else if (ra == 0) then
        c = -sgn
        return
      else if (rb == 0) then
        c = sgn
        return
      end if
      ! ra/ad < rb/bd  <=>  ad/ra > bd/rb
      an = ad; ad = ra; bn = bd; bd = rb
      sgn = -sgn
    end do
  end function

  function rat_add_rr(a, b) result(r)
    type(rational), intent(in) :: a, b
    type(rational) :: r
    integer(c_int32_t) :: st
    st = zxv_rat_add(a%n, a%d, b%n, b%d, r%n, r%d)
    if (st /= ZXV_OK) call rat_refused(st, '+', a, b)
  end function

  function rat_add_ri32(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    type(rational) :: r
    r = rat_add_rr(a, rational(int(b, int64)))
  end function
  function rat_add_i32r(a, b) result(r)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_add_rr(rational(int(a, int64)), b)
  end function

  function rat_add_ri64(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    type(rational) :: r
    r = rat_add_rr(a, rational(int(b, int64)))
  end function
  function rat_add_i64r(a, b) result(r)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_add_rr(rational(int(a, int64)), b)
  end function

  function rat_sub_rr(a, b) result(r)
    type(rational), intent(in) :: a, b
    type(rational) :: r
    integer(c_int32_t) :: st
    st = zxv_rat_sub(a%n, a%d, b%n, b%d, r%n, r%d)
    if (st /= ZXV_OK) call rat_refused(st, '-', a, b)
  end function

  function rat_sub_ri32(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    type(rational) :: r
    r = rat_sub_rr(a, rational(int(b, int64)))
  end function
  function rat_sub_i32r(a, b) result(r)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_sub_rr(rational(int(a, int64)), b)
  end function

  function rat_sub_ri64(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    type(rational) :: r
    r = rat_sub_rr(a, rational(int(b, int64)))
  end function
  function rat_sub_i64r(a, b) result(r)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_sub_rr(rational(int(a, int64)), b)
  end function

  function rat_mul_rr(a, b) result(r)
    type(rational), intent(in) :: a, b
    type(rational) :: r
    integer(c_int32_t) :: st
    st = zxv_rat_mul(a%n, a%d, b%n, b%d, r%n, r%d)
    if (st /= ZXV_OK) call rat_refused(st, '*', a, b)
  end function

  function rat_mul_ri32(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    type(rational) :: r
    r = rat_mul_rr(a, rational(int(b, int64)))
  end function
  function rat_mul_i32r(a, b) result(r)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_mul_rr(rational(int(a, int64)), b)
  end function

  function rat_mul_ri64(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    type(rational) :: r
    r = rat_mul_rr(a, rational(int(b, int64)))
  end function
  function rat_mul_i64r(a, b) result(r)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_mul_rr(rational(int(a, int64)), b)
  end function

  function rat_div_rr(a, b) result(r)
    type(rational), intent(in) :: a, b
    type(rational) :: r
    integer(c_int32_t) :: st
    st = zxv_rat_div(a%n, a%d, b%n, b%d, r%n, r%d)
    if (st /= ZXV_OK) call rat_refused(st, '/', a, b)
  end function

  function rat_div_ri32(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    type(rational) :: r
    r = rat_div_rr(a, rational(int(b, int64)))
  end function
  function rat_div_i32r(a, b) result(r)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_div_rr(rational(int(a, int64)), b)
  end function

  function rat_div_ri64(a, b) result(r)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    type(rational) :: r
    r = rat_div_rr(a, rational(int(b, int64)))
  end function
  function rat_div_i64r(a, b) result(r)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    type(rational) :: r
    r = rat_div_rr(rational(int(a, int64)), b)
  end function

  ! Comparisons are pure: they cannot fail on two valid rationals, so they
  ! need no status and no stop. rat_order compares an/ad with bn/bd (ad, bd
  ! > 0) by their continued fractions, which never overflows int64; the
  ! integer comparands go in as n/1 directly, so -2**63 compares too.

  pure logical function rat_eq_rr(a, b) result(t)
    type(rational), intent(in) :: a, b
    t = rat_order(a%n, a%d, b%n, b%d) == 0
  end function
  pure logical function rat_eq_ri32(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    t = rat_order(a%n, a%d, int(b, int64), 1_int64) == 0
  end function
  pure logical function rat_eq_i32r(a, b) result(t)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(int(a, int64), 1_int64, b%n, b%d) == 0
  end function
  pure logical function rat_eq_ri64(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    t = rat_order(a%n, a%d, b, 1_int64) == 0
  end function
  pure logical function rat_eq_i64r(a, b) result(t)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(a, 1_int64, b%n, b%d) == 0
  end function

  pure logical function rat_ne_rr(a, b) result(t)
    type(rational), intent(in) :: a, b
    t = rat_order(a%n, a%d, b%n, b%d) /= 0
  end function
  pure logical function rat_ne_ri32(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    t = rat_order(a%n, a%d, int(b, int64), 1_int64) /= 0
  end function
  pure logical function rat_ne_i32r(a, b) result(t)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(int(a, int64), 1_int64, b%n, b%d) /= 0
  end function
  pure logical function rat_ne_ri64(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    t = rat_order(a%n, a%d, b, 1_int64) /= 0
  end function
  pure logical function rat_ne_i64r(a, b) result(t)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(a, 1_int64, b%n, b%d) /= 0
  end function

  pure logical function rat_lt_rr(a, b) result(t)
    type(rational), intent(in) :: a, b
    t = rat_order(a%n, a%d, b%n, b%d) < 0
  end function
  pure logical function rat_lt_ri32(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    t = rat_order(a%n, a%d, int(b, int64), 1_int64) < 0
  end function
  pure logical function rat_lt_i32r(a, b) result(t)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(int(a, int64), 1_int64, b%n, b%d) < 0
  end function
  pure logical function rat_lt_ri64(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    t = rat_order(a%n, a%d, b, 1_int64) < 0
  end function
  pure logical function rat_lt_i64r(a, b) result(t)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(a, 1_int64, b%n, b%d) < 0
  end function

  pure logical function rat_le_rr(a, b) result(t)
    type(rational), intent(in) :: a, b
    t = rat_order(a%n, a%d, b%n, b%d) <= 0
  end function
  pure logical function rat_le_ri32(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    t = rat_order(a%n, a%d, int(b, int64), 1_int64) <= 0
  end function
  pure logical function rat_le_i32r(a, b) result(t)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(int(a, int64), 1_int64, b%n, b%d) <= 0
  end function
  pure logical function rat_le_ri64(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    t = rat_order(a%n, a%d, b, 1_int64) <= 0
  end function
  pure logical function rat_le_i64r(a, b) result(t)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(a, 1_int64, b%n, b%d) <= 0
  end function

  pure logical function rat_gt_rr(a, b) result(t)
    type(rational), intent(in) :: a, b
    t = rat_order(a%n, a%d, b%n, b%d) > 0
  end function
  pure logical function rat_gt_ri32(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    t = rat_order(a%n, a%d, int(b, int64), 1_int64) > 0
  end function
  pure logical function rat_gt_i32r(a, b) result(t)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(int(a, int64), 1_int64, b%n, b%d) > 0
  end function
  pure logical function rat_gt_ri64(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    t = rat_order(a%n, a%d, b, 1_int64) > 0
  end function
  pure logical function rat_gt_i64r(a, b) result(t)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(a, 1_int64, b%n, b%d) > 0
  end function

  pure logical function rat_ge_rr(a, b) result(t)
    type(rational), intent(in) :: a, b
    t = rat_order(a%n, a%d, b%n, b%d) >= 0
  end function
  pure logical function rat_ge_ri32(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: b
    t = rat_order(a%n, a%d, int(b, int64), 1_int64) >= 0
  end function
  pure logical function rat_ge_i32r(a, b) result(t)
    integer(int32), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(int(a, int64), 1_int64, b%n, b%d) >= 0
  end function
  pure logical function rat_ge_ri64(a, b) result(t)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: b
    t = rat_order(a%n, a%d, b, 1_int64) >= 0
  end function
  pure logical function rat_ge_i64r(a, b) result(t)
    integer(int64), intent(in) :: a
    type(rational), intent(in) :: b
    t = rat_order(a, 1_int64, b%n, b%d) >= 0
  end function

  function rat_neg(a) result(r)
    type(rational), intent(in) :: a
    type(rational) :: r
    r = rat_sub_rr(rational(0), a)
  end function

  function rat_abs(a) result(r)
    type(rational), intent(in) :: a
    type(rational) :: r
    r = a
    if (a%n < 0) r = rat_neg(a)
  end function

  function rat_pow_i64(a, e) result(r)
    type(rational), intent(in) :: a
    integer(int64), intent(in) :: e
    type(rational) :: r, b
    integer(int64) :: m
    r = rational(1)
    b = a
    if (e < 0 .and. a%n == 0) then
      write (error_unit, '(a)') 'zxv: rational 0 ** ' // itoa(e) // ' divides by zero'
      flush (error_unit)
      error stop 1
    end if
    if (a%n == a%d .or. a%n == -a%d) then
      ! 1 or -1: the sign is the parity of e (two's complement keeps it)
      if (iand(e, 1_int64) == 1 .and. a%n < 0) r = rational(-1)
      return
    end if
    if (e == -huge(e) - 1) call rat_refused(ZXV_E_RANGE, '**', a, rational(e))
    m = abs(e)
    do while (m > 0)
      if (iand(m, 1_int64) == 1) r = rat_mul_rr(r, b)
      m = shiftr(m, 1)
      if (m > 0) b = rat_mul_rr(b, b)
    end do
    if (e < 0) r = rat_div_rr(rational(1), r)
  end function

  function rat_pow_i32(a, e) result(r)
    type(rational), intent(in) :: a
    integer(int32), intent(in) :: e
    type(rational) :: r
    r = rat_pow_i64(a, int(e, int64))
  end function

  subroutine rat_assign_i32(lhs, rhs)
    type(rational), intent(out) :: lhs
    integer(int32), intent(in) :: rhs
    lhs = rat_from_i32(rhs)
  end subroutine

  subroutine rat_assign_i64(lhs, rhs)
    type(rational), intent(out) :: lhs
    integer(int64), intent(in) :: rhs
    lhs = rat_from_i64(rhs)
  end subroutine

  !> r = a + b with a stat (ZXV_E_RANGE when it does not fit; r unchanged then).
  subroutine rational_add(a, b, r, stat)
    type(rational), intent(in) :: a, b
    type(rational), intent(inout) :: r
    integer(int32), intent(out), optional :: stat
    call rat_op(1, a, b, r, stat)
  end subroutine

  subroutine rational_sub(a, b, r, stat)
    type(rational), intent(in) :: a, b
    type(rational), intent(inout) :: r
    integer(int32), intent(out), optional :: stat
    call rat_op(2, a, b, r, stat)
  end subroutine

  subroutine rational_mul(a, b, r, stat)
    type(rational), intent(in) :: a, b
    type(rational), intent(inout) :: r
    integer(int32), intent(out), optional :: stat
    call rat_op(3, a, b, r, stat)
  end subroutine

  !> r = a / b; b = 0 is ZXV_E_ARG.
  subroutine rational_div(a, b, r, stat)
    type(rational), intent(in) :: a, b
    type(rational), intent(inout) :: r
    integer(int32), intent(out), optional :: stat
    call rat_op(4, a, b, r, stat)
  end subroutine

  subroutine rat_op(k, a, b, r, stat)
    integer, intent(in) :: k
    type(rational), intent(in) :: a, b
    type(rational), intent(inout) :: r
    integer(int32), intent(out), optional :: stat
    integer(c_int64_t) :: n, d
    integer(c_int32_t) :: st
    select case (k)
    case (1)
      st = zxv_rat_add(a%n, a%d, b%n, b%d, n, d)
    case (2)
      st = zxv_rat_sub(a%n, a%d, b%n, b%d, n, d)
    case (3)
      st = zxv_rat_mul(a%n, a%d, b%n, b%d, n, d)
    case default
      st = zxv_rat_div(a%n, a%d, b%n, b%d, n, d)
    end select
    call handle(st, 'rational arithmetic', stat)
    if (st /= ZXV_OK) return
    r%n = n
    r%d = d
  end subroutine

  !> The exact sum of v, in order (what Trebago's sum(i, v(i)) computes).
  function rational_sum(v, stat) result(s)
    type(rational), intent(in) :: v(:)
    integer(int32), intent(out), optional :: stat
    type(rational) :: s
    integer :: i
    integer(int32) :: st
    s = rational(0)
    if (present(stat)) stat = ZXV_OK
    do i = 1, size(v)
      call rational_add(s, v(i), s, st)
      if (st /= ZXV_OK) then
        if (present(stat)) then
          stat = st
          return
        end if
        call handle(st, 'rational_sum')
      end if
    end do
  end function

  !> The exact dot product of a and b (same length), in order.
  function rational_dot(a, b, stat) result(s)
    type(rational), intent(in) :: a(:), b(:)
    integer(int32), intent(out), optional :: stat
    type(rational) :: s, t
    integer :: i
    integer(int32) :: st
    s = rational(0)
    if (present(stat)) stat = ZXV_OK
    if (size(a) /= size(b)) then
      call handle(ZXV_E_ARG, 'rational_dot (lengths differ)', stat)
      return
    end if
    do i = 1, size(a)
      call rational_mul(a(i), b(i), t, st)
      if (st == ZXV_OK) call rational_add(s, t, s, st)
      if (st /= ZXV_OK) then
        if (present(stat)) then
          stat = st
          return
        end if
        call handle(st, 'rational_dot')
      end if
    end do
  end function

  !> Numerators and denominators of v, for Trebago or for storage.
  subroutine rational_split(v, num, den)
    type(rational), intent(in) :: v(:)
    integer(int64), intent(out) :: num(size(v)), den(size(v))
    integer :: i
    do i = 1, size(v)
      num(i) = v(i)%n
      den(i) = v(i)%d
    end do
  end subroutine

  !> Rationals from numerators and denominators (normalized; a zero denominator stops).
  function rational_join(num, den) result(v)
    integer(int64), intent(in) :: num(:), den(:)
    type(rational) :: v(size(num))
    integer :: i
    do i = 1, size(num)
      v(i) = rat_make(num(i), den(i), 'rational_join')
    end do
  end function

  ! ===================================================================
  ! truth
  ! ===================================================================

  !> The truth with kernel value 0..3 (ZXV_E_ARG otherwise).
  function truth_of(v, stat) result(t)
    integer(int32), intent(in) :: v
    integer(int32), intent(out), optional :: stat
    type(truth) :: t
    if (v < 0 .or. v > 3) then
      call handle(ZXV_E_ARG, 'truth_of', stat)
      return
    end if
    if (present(stat)) stat = ZXV_OK
    t%v = v
  end function

  !> BOTH when there is evidence for and against, TRUE / FALSE for one side, else NEITHER.
  function truth_from_evidence(positive, negative) result(t)
    integer(int64), intent(in) :: positive, negative
    type(truth) :: t
    call handle(c_lp_from_evidence(u32(positive), u32(negative), t%v), 'truth_from_evidence')
  end function

  integer(int32) function truth_value(self)
    class(truth), intent(in) :: self
    truth_value = self%v
  end function

  function truth_name(self) result(s)
    class(truth), intent(in) :: self
    character(len=:), allocatable :: s
    s = from_cstr(c_lp_name(self%v))
  end function

  pure logical function truth_is_true(self)
    class(truth), intent(in) :: self
    truth_is_true = self%v == 1
  end function

  pure logical function truth_is_false(self)
    class(truth), intent(in) :: self
    truth_is_false = self%v == 2
  end function

  pure logical function truth_is_both(self)
    class(truth), intent(in) :: self
    truth_is_both = self%v == 3
  end function

  pure logical function truth_is_neither(self)
    class(truth), intent(in) :: self
    truth_is_neither = self%v == 0
  end function

  !> The only way to a LOGICAL: say what BOTH and NEITHER mean here.
  pure logical function truth_to_logical(self, on_both, on_neither)
    class(truth), intent(in) :: self
    logical, intent(in) :: on_both, on_neither
    select case (self%v)
    case (1)
      truth_to_logical = .true.
    case (2)
      truth_to_logical = .false.
    case (3)
      truth_to_logical = on_both
    case default
      truth_to_logical = on_neither
    end select
  end function

  function truth_and(a, b) result(t)
    type(truth), intent(in) :: a, b
    type(truth) :: t
    call handle(c_lp_and(a%v, b%v, t%v), 'truth .and.')
  end function

  function truth_or(a, b) result(t)
    type(truth), intent(in) :: a, b
    type(truth) :: t
    call handle(c_lp_or(a%v, b%v, t%v), 'truth .or.')
  end function

  function truth_not(a) result(t)
    type(truth), intent(in) :: a
    type(truth) :: t
    call handle(c_lp_not(a%v, t%v), 'truth .not.')
  end function

  pure logical function truth_eq(a, b)
    type(truth), intent(in) :: a, b
    truth_eq = a%v == b%v
  end function

  pure logical function truth_ne(a, b)
    type(truth), intent(in) :: a, b
    truth_ne = a%v /= b%v
  end function

  ! ===================================================================
  ! hexagram
  ! ===================================================================

  function hex_from_fields(base, polarity, phase, stat) result(h)
    integer(int32), intent(in) :: base, polarity, phase
    integer(int32), intent(out), optional :: stat
    type(hexagram) :: h
    integer(c_int8_t) :: t
    integer(c_int32_t) :: st
    if (min(base, polarity, phase) < 0 .or. max(base, polarity, phase) > 3) then
      call handle(ZXV_E_ARG, 'hexagram(base, polarity, phase)', stat)
      return
    end if
    st = c_hex_pack(u8(base), u8(polarity), u8(phase), t)
    call handle(st, 'hexagram(base, polarity, phase)', stat)
    if (st == ZXV_OK) h%t = from_u8(t)
  end function

  !> A tag 0..63 (ZXV_E_ARG above 63).
  function hexagram_from_tag(tag, stat) result(h)
    integer(int32), intent(in) :: tag
    integer(int32), intent(out), optional :: stat
    type(hexagram) :: h
    integer(c_int8_t) :: b, p, ph
    integer(c_int32_t) :: st
    if (tag < 0 .or. tag > 255) then
      call handle(ZXV_E_ARG, 'hexagram_from_tag', stat)
      return
    end if
    st = c_hex_unpack(u8(tag), b, p, ph)
    call handle(st, 'hexagram_from_tag', stat)
    if (st == ZXV_OK) h%t = tag
  end function

  !> The base for a truth (explicit: the numbering differs), with a polarity and phase.
  function hexagram_from_truth(t, polarity, phase, stat) result(h)
    type(truth), intent(in) :: t
    integer(int32), intent(in) :: polarity, phase
    integer(int32), intent(out), optional :: stat
    type(hexagram) :: h
    integer(c_int8_t) :: b
    integer(c_int32_t) :: st
    st = c_hex_from_lp(t%v, b)
    call handle(st, 'hexagram_from_truth', stat)
    if (st /= ZXV_OK) return
    h = hex_from_fields(from_u8(b), polarity, phase, stat)
  end function

  !> The phase after `phase` on the Gray cycle.
  integer(int32) function hex_phase_next(phase)
    integer(int32), intent(in) :: phase
    integer(c_int8_t) :: o
    call handle(c_hex_phase_next(u8(phase), o), 'hex_phase_next')
    hex_phase_next = from_u8(o)
  end function

  !> Polarity from evidence counts before and after (never FORBIDDEN).
  integer(int32) function hex_polarity_from_evidence(pos0, neg0, pos1, neg1, stale)
    integer(int64), intent(in) :: pos0, neg0, pos1, neg1
    logical, intent(in) :: stale
    integer(c_int8_t) :: o
    integer(c_int32_t) :: s
    s = merge(1, 0, stale)
    call handle(c_hex_polarity(u32(pos0), u32(neg0), u32(pos1), u32(neg1), s, o), 'hex_polarity_from_evidence')
    hex_polarity_from_evidence = from_u8(o)
  end function

  integer(int32) function hex_tag(self)
    class(hexagram), intent(in) :: self
    hex_tag = self%t
  end function

  integer(int32) function hex_base(self)
    class(hexagram), intent(in) :: self
    hex_base = shiftr(self%t, 4)
  end function

  integer(int32) function hex_polarity_of(self)
    class(hexagram), intent(in) :: self
    hex_polarity_of = iand(shiftr(self%t, 2), 3)
  end function

  integer(int32) function hex_phase(self)
    class(hexagram), intent(in) :: self
    hex_phase = iand(self%t, 3)
  end function

  logical function hex_is_forbidden(self)
    class(hexagram), intent(in) :: self
    hex_is_forbidden = hex_polarity_of(self) == HEX_POL_FORBIDDEN
  end function

  !> HEX_ACT_PROCEED, _WAIT, _REFUSE, _ESCALATE or _ROLLBACK.
  integer(int32) function hex_action(self)
    class(hexagram), intent(in) :: self
    integer(c_int32_t) :: a
    call handle(c_hex_action(u8(self%t), a), 'hexagram%action')
    hex_action = a
  end function

  function hex_action_name(self) result(s)
    class(hexagram), intent(in) :: self
    character(len=:), allocatable :: s
    s = from_cstr(c_hex_action_name(hex_action(self)))
  end function

  function hex_name(self) result(s)
    class(hexagram), intent(in) :: self
    character(len=:), allocatable :: s
    s = from_cstr(c_hex_name(u8(self%t)))
  end function

  !> Move to `phase`: the same or the next one only (ZXV_E_STATE for a jump).
  function hex_step(self, phase, stat) result(h)
    class(hexagram), intent(in) :: self
    integer(int32), intent(in) :: phase
    integer(int32), intent(out), optional :: stat
    type(hexagram) :: h
    integer(c_int8_t) :: o
    integer(c_int32_t) :: st
    h%t = self%t
    if (phase < 0 .or. phase > 3) then
      call handle(ZXV_E_ARG, 'hexagram%step', stat)
      return
    end if
    st = c_hex_step(u8(self%t), u8(phase), o)
    call handle(st, 'hexagram%step', stat)
    if (st == ZXV_OK) h%t = from_u8(o)
  end function

  !> The next phase; cannot fail.
  function hex_advance(self) result(h)
    class(hexagram), intent(in) :: self
    type(hexagram) :: h
    h = hex_step(self, hex_phase_next(hex_phase(self)))
  end function

  !> New base and polarity, same phase. A forbidden tag refuses to be un-forbidden (ZXV_E_STATE).
  function hex_with_state(self, base, polarity, stat) result(h)
    class(hexagram), intent(in) :: self
    integer(int32), intent(in) :: base, polarity
    integer(int32), intent(out), optional :: stat
    type(hexagram) :: h
    integer(c_int8_t) :: o
    integer(c_int32_t) :: st
    h%t = self%t
    if (min(base, polarity) < 0 .or. max(base, polarity) > 3) then
      call handle(ZXV_E_ARG, 'hexagram%with_state', stat)
      return
    end if
    st = c_hex_set(u8(self%t), u8(base), u8(polarity), o)
    call handle(st, 'hexagram%with_state', stat)
    if (st == ZXV_OK) h%t = from_u8(o)
  end function

  function hex_forbid(self) result(h)
    class(hexagram), intent(in) :: self
    type(hexagram) :: h
    integer(c_int8_t) :: o
    call handle(c_hex_forbid(u8(self%t), o), 'hexagram%forbid')
    h%t = from_u8(o)
  end function

  !> The base as a truth (explicit: TRUE is base 3 but truth 1).
  function hex_to_truth(self) result(t)
    class(hexagram), intent(in) :: self
    type(truth) :: t
    call handle(c_hex_to_lp(u8(hex_base(self)), t%v), 'hexagram%to_truth')
  end function

  function hex_and(a, b) result(h)
    type(hexagram), intent(in) :: a, b
    type(hexagram) :: h
    integer(c_int8_t) :: o
    call handle(c_hex_and(u8(a%t), u8(b%t), o), 'hexagram .and.')
    h%t = from_u8(o)
  end function

  function hex_or(a, b) result(h)
    type(hexagram), intent(in) :: a, b
    type(hexagram) :: h
    integer(c_int8_t) :: o
    call handle(c_hex_or(u8(a%t), u8(b%t), o), 'hexagram .or.')
    h%t = from_u8(o)
  end function

  function hex_not(a) result(h)
    type(hexagram), intent(in) :: a
    type(hexagram) :: h
    integer(c_int8_t) :: o
    call handle(c_hex_not(u8(a%t), o), 'hexagram .not.')
    h%t = from_u8(o)
  end function

  pure logical function hex_eq(a, b)
    type(hexagram), intent(in) :: a, b
    hex_eq = a%t == b%t
  end function

  pure logical function hex_ne(a, b)
    type(hexagram), intent(in) :: a, b
    hex_ne = a%t /= b%t
  end function

end module zxv
