! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! trebago.f90 - run Trebago programs on Fortran arrays (libztrebago,
! include/ztrebago.h).
!
!   type(trebago_program) :: p
!   integer(int64), target :: xn(3), xd(3), sn, sd
!   call p%parse('trebago s' // nl // ' in x(n)' // nl // ' out s' // nl // &
!                ' s = sum(i, x(i))' // nl // 'end')
!   call p%bind('x', xn, xd)      ! numerators and denominators, any rank 0..2
!   call p%bind('s', sn, sd)
!   call p%run()                  ! exact; ERROR STOP on a refusal without stat=
!
! Values cross as INTEGER(INT64) numerator and denominator arrays of the
! same shape, in Fortran's own column-major order: no copy, no REAL. The
! arrays must be contiguous and must stay alive (declare them TARGET, or
! ALLOCATABLE, TARGET) until run returns; bind keeps their addresses. Out
! and inout arrays are written only when the whole program succeeded.
!
! Errors follow module zxv: with stat= the status comes back (and the
! diagnostic in message=); without it a refusal is ERROR STOP 1 with the
! diagnostic. Trebago's own codes are TB_E_* (-2101 .. -2106); exact
! overflow is ZXV_E_RANGE.
module trebago
  use, intrinsic :: iso_c_binding
  use, intrinsic :: iso_fortran_env, only: int32, int64, error_unit
  use zxv, only: ZXV_OK, ZXV_E_ARG, ZXV_E_IO, ZXV_CLASS_FATAL, ZXV_FATAL_EXIT
  implicit none
  private

  integer(int32), parameter, public :: TB_E_SYNTAX = -2101, TB_E_NAME = -2102, &
    TB_E_SHAPE = -2103, TB_E_BOUNDS = -2104, TB_E_DIVZERO = -2105, TB_E_LIMIT = -2106

  ! include/ztrebago.h: tb_array and tb_diag, interoperable
  type, bind(c) :: tb_array_c
    type(c_ptr) :: name = c_null_ptr
    type(c_ptr) :: num = c_null_ptr
    type(c_ptr) :: den = c_null_ptr
    integer(c_int32_t) :: rank = 0
    integer(c_int32_t) :: reserved = 0
    integer(c_int64_t) :: extent(2) = 0
  end type

  type, bind(c) :: tb_diag_c
    integer(c_int32_t) :: line = 0, col = 0
    integer(c_int64_t) :: offset = 0
    character(kind=c_char) :: msg(256) = c_null_char
  end type

  type :: binding
    character(kind=c_char) :: name(32) = c_null_char
    type(c_ptr) :: num = c_null_ptr, den = c_null_ptr
    integer(c_int32_t) :: rank = 0
    integer(c_int64_t) :: extent(2) = 0
  end type

  !> A parsed Trebago program and the arrays bound to it.
  type, public :: trebago_program
    private
    type(c_ptr) :: h = c_null_ptr
    type(binding), allocatable :: b(:)
    integer :: nb = 0
    integer(int64) :: limit = 0
  contains
    procedure :: parse => tbp_parse
    procedure :: load => tbp_load
    procedure, private :: bind0, bind1, bind2
    generic :: bind => bind0, bind1, bind2
    procedure :: clear_bindings => tbp_clear
    procedure :: run => tbp_run
    procedure :: text => tbp_text
    procedure :: digest => tbp_digest
    procedure :: set_work_limit => tbp_set_limit
    procedure :: is_parsed => tbp_is_parsed
    procedure :: destroy => tbp_destroy
    procedure, private :: tbp_assign
    generic :: assignment(=) => tbp_assign
    final :: tbp_final
  end type

  public :: trebago_last_work, trebago_status_name

  interface
    integer(c_int32_t) function tb_parse(text, len, out, diag) bind(c, name='tb_parse')
      import :: c_int32_t, c_size_t, c_char, c_ptr, tb_diag_c
      character(kind=c_char), dimension(*), intent(in) :: text
      integer(c_size_t), value :: len
      type(c_ptr), intent(out) :: out
      type(tb_diag_c), intent(out) :: diag
    end function
    subroutine tb_program_destroy(p) bind(c, name='tb_program_destroy')
      import :: c_ptr
      type(c_ptr), value :: p
    end subroutine
    integer(c_int32_t) function tb_program_text(p, buf, cap, out_len) bind(c, name='tb_program_text')
      import :: c_int32_t, c_ptr, c_size_t
      type(c_ptr), value :: p, buf
      integer(c_size_t), value :: cap
      integer(c_size_t), intent(out) :: out_len
    end function
    integer(c_int32_t) function tb_program_digest(p, out) bind(c, name='tb_program_digest')
      import :: c_int32_t, c_ptr, c_int8_t
      type(c_ptr), value :: p
      integer(c_int8_t), dimension(32), intent(out) :: out
    end function
    integer(c_int32_t) function tb_exec(p, arrays, count, limit, diag) bind(c, name='tb_exec')
      import :: c_int32_t, c_int64_t, c_ptr, tb_array_c, tb_diag_c
      type(c_ptr), value :: p
      type(tb_array_c), dimension(*), intent(in) :: arrays
      integer(c_int32_t), value :: count
      integer(c_int64_t), value :: limit
      type(tb_diag_c), intent(out) :: diag
    end function
    type(c_ptr) function tb_status_name(st) bind(c, name='tb_status_name')
      import :: c_ptr, c_int32_t
      integer(c_int32_t), value :: st
    end function
    integer(c_int32_t) function tb_status_class(st) bind(c, name='tb_status_class')
      import :: c_int32_t
      integer(c_int32_t), value :: st
    end function
    integer(c_int32_t) function tb_last_work(out) bind(c, name='tb_last_work')
      import :: c_int32_t, c_int64_t
      integer(c_int64_t), intent(out) :: out
    end function
    integer(c_size_t) function c_strlen(s) bind(c, name='strlen')
      import :: c_size_t, c_ptr
      type(c_ptr), value :: s
    end function
  end interface

contains

  !> "TB_E_SHAPE", or libzxv's name for its own codes.
  function trebago_status_name(st) result(s)
    integer(int32), intent(in) :: st
    character(len=:), allocatable :: s
    character(kind=c_char), pointer :: a(:)
    type(c_ptr) :: p
    integer :: n, i
    p = tb_status_name(st)
    n = int(c_strlen(p))
    call c_f_pointer(p, a, [n])
    allocate(character(len=n) :: s)
    do i = 1, n
      s(i:i) = a(i)
    end do
  end function

  !> Rational operations the last run on this thread performed.
  integer(int64) function trebago_last_work()
    integer(c_int32_t) :: st
    trebago_last_work = 0
    st = tb_last_work(trebago_last_work)
  end function

  function diag_text(d) result(s)
    type(tb_diag_c), intent(in) :: d
    character(len=:), allocatable :: s
    integer :: n
    n = 0
    do while (n < 256)
      if (d%msg(n + 1) == c_null_char) exit
      n = n + 1
    end do
    allocate(character(len=n) :: s)
    s = transfer(d%msg(1:n), s)
  end function

  subroutine report(st, msg, what, stat, message)
    integer(c_int32_t), intent(in) :: st
    character(len=*), intent(in) :: msg, what
    integer(int32), intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: message
    if (present(stat)) stat = st
    if (present(message)) message = msg
    if (st == ZXV_OK) return
    if (tb_status_class(st) == ZXV_CLASS_FATAL) then
      write (error_unit, '(a)') 'trebago: FATAL ' // trebago_status_name(st) // ' in ' // what // ': ' // msg
      flush (error_unit)
      error stop ZXV_FATAL_EXIT
    end if
    if (.not. present(stat)) then
      write (error_unit, '(a)') 'trebago: ' // what // ' refused: ' // msg // ' (pass stat= to handle it)'
      flush (error_unit)
      error stop 1
    end if
  end subroutine

  !> Parse Trebago text. Replaces any program (and bindings) held before.
  subroutine tbp_parse(self, text, stat, message)
    class(trebago_program), intent(inout) :: self
    character(len=*), intent(in) :: text
    integer(int32), intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: message
    type(tb_diag_c) :: d
    integer(c_int32_t) :: st
    type(c_ptr) :: h
    call tbp_destroy(self)
    st = tb_parse(text, int(len(text), c_size_t), h, d)
    call report(st, diag_text(d), 'parse', stat, message)
    if (st == ZXV_OK) self%h = h
  end subroutine

  !> Parse a Trebago file (ZXV_E_IO if it cannot be read).
  subroutine tbp_load(self, path, stat, message)
    class(trebago_program), intent(inout) :: self
    character(len=*), intent(in) :: path
    integer(int32), intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: message
    character(len=:), allocatable :: text
    integer :: u, ios, sz
    open (newunit=u, file=path, access='stream', form='unformatted', status='old', action='read', iostat=ios)
    if (ios == 0) inquire (unit=u, size=sz, iostat=ios)
    if (ios == 0) then
      allocate(character(len=sz) :: text)
      if (sz > 0) read (u, iostat=ios) text
      close (u)
    end if
    if (ios /= 0) then
      call report(ZXV_E_IO, 'cannot read ' // path, 'load', stat, message)
      return
    end if
    call tbp_parse(self, text, stat, message)
  end subroutine

  logical function tbp_is_parsed(self)
    class(trebago_program), intent(in) :: self
    tbp_is_parsed = c_associated(self%h)
  end function

  subroutine add_binding(self, name, num, den, rank, extent)
    class(trebago_program), intent(inout) :: self
    character(len=*), intent(in) :: name
    type(c_ptr), intent(in) :: num, den
    integer, intent(in) :: rank
    integer(int64), intent(in) :: extent(2)
    type(binding), allocatable :: grown(:)
    integer :: k, i, n
    character(len=:), allocatable :: nm
    nm = trim(name)
    if (len(nm) > 31) call bind_refused(nm, 'a Trebago name has at most 31 characters')
    n = len(nm)
    ! re-binding a name replaces it
    k = 0
    do i = 1, self%nb
      if (same_name(self%b(i)%name, nm)) k = i
    end do
    if (k == 0) then
      if (.not. allocated(self%b)) allocate(self%b(8))
      if (self%nb == size(self%b)) then
        allocate(grown(2 * size(self%b)))
        grown(1:self%nb) = self%b(1:self%nb)
        call move_alloc(grown, self%b)
      end if
      self%nb = self%nb + 1
      k = self%nb
    end if
    self%b(k)%name = c_null_char
    do i = 1, n
      self%b(k)%name(i) = nm(i:i)
    end do
    self%b(k)%num = num
    self%b(k)%den = den
    self%b(k)%rank = rank
    self%b(k)%extent = extent
  end subroutine

  logical function same_name(a, b)
    character(kind=c_char), intent(in) :: a(32)
    character(len=*), intent(in) :: b
    integer :: i
    same_name = .false.
    if (len(b) > 31) return
    do i = 1, len(b)
      if (a(i) /= b(i:i)) return
    end do
    same_name = a(len(b) + 1) == c_null_char
  end function

  !> Bind a scalar parameter: one numerator and one denominator.
  subroutine bind0(self, name, num, den)
    class(trebago_program), intent(inout) :: self
    character(len=*), intent(in) :: name
    integer(int64), intent(inout), target :: num, den
    call add_binding(self, name, c_loc(num), c_loc(den), 0, [0_int64, 0_int64])
  end subroutine

  !> Bind a vector parameter: numerators and denominators of the same length.
  subroutine bind1(self, name, num, den)
    class(trebago_program), intent(inout) :: self
    character(len=*), intent(in) :: name
    integer(int64), intent(inout), target :: num(:), den(:)
    type(c_ptr) :: pn, pd
    if (size(num) /= size(den)) call bind_refused(name, 'numerator and denominator arrays differ in length')
    if (.not. (is_contiguous(num) .and. is_contiguous(den))) &
      call bind_refused(name, 'the arrays must be contiguous (pass the whole array, not a strided section)')
    pn = c_null_ptr
    pd = c_null_ptr
    if (size(num) > 0) then
      pn = c_loc(num)
      pd = c_loc(den)
    end if
    call add_binding(self, name, pn, pd, 1, [int(size(num), int64), 0_int64])
  end subroutine

  !> Bind a matrix parameter, column-major as Fortran stores it: a(i, j).
  subroutine bind2(self, name, num, den)
    class(trebago_program), intent(inout) :: self
    character(len=*), intent(in) :: name
    integer(int64), intent(inout), target :: num(:, :), den(:, :)
    type(c_ptr) :: pn, pd
    if (any(shape(num) /= shape(den))) call bind_refused(name, 'numerator and denominator arrays differ in shape')
    if (.not. (is_contiguous(num) .and. is_contiguous(den))) &
      call bind_refused(name, 'the arrays must be contiguous (pass the whole array, not a strided section)')
    pn = c_null_ptr
    pd = c_null_ptr
    if (size(num) > 0) then
      pn = c_loc(num)
      pd = c_loc(den)
    end if
    call add_binding(self, name, pn, pd, 2, [int(size(num, 1), int64), int(size(num, 2), int64)])
  end subroutine

  subroutine bind_refused(name, why)
    character(len=*), intent(in) :: name, why
    write (error_unit, '(a)') 'trebago: bind ' // trim(name) // ': ' // why
    flush (error_unit)
    error stop 1
  end subroutine

  subroutine tbp_clear(self)
    class(trebago_program), intent(inout) :: self
    self%nb = 0
  end subroutine

  !> At most `limit` rational operations per run (0: the library default, 2**32).
  subroutine tbp_set_limit(self, limit)
    class(trebago_program), intent(inout) :: self
    integer(int64), intent(in) :: limit
    self%limit = limit
  end subroutine

  !> Run the program on the bound arrays. Exact: an overflow is ZXV_E_RANGE, never rounded.
  subroutine tbp_run(self, stat, message)
    class(trebago_program), intent(inout), target :: self
    integer(int32), intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: message
    type(tb_array_c), allocatable :: arr(:)
    type(tb_diag_c) :: d
    integer(c_int32_t) :: st
    integer :: i
    if (.not. c_associated(self%h)) then
      call report(ZXV_E_ARG, 'no program: call parse or load first', 'run', stat, message)
      return
    end if
    allocate(arr(max(self%nb, 1)))
    do i = 1, self%nb
      arr(i)%name = c_loc(self%b(i)%name)
      arr(i)%num = self%b(i)%num
      arr(i)%den = self%b(i)%den
      arr(i)%rank = self%b(i)%rank
      arr(i)%reserved = 0
      arr(i)%extent = self%b(i)%extent
    end do
    st = tb_exec(self%h, arr, int(self%nb, c_int32_t), int(self%limit, c_int64_t), d)
    call report(st, diag_text(d), 'run', stat, message)
  end subroutine

  !> The canonical Trebago text.
  function tbp_text(self) result(s)
    class(trebago_program), intent(in) :: self
    character(len=:), allocatable :: s
    character(kind=c_char), allocatable, target :: buf(:)
    integer(c_size_t) :: need, room
    integer(c_int32_t) :: st
    s = ''
    if (.not. c_associated(self%h)) return
    need = 0
    st = tb_program_text(self%h, c_null_ptr, 0_c_size_t, need)
    room = need + 1
    allocate(buf(room))
    st = tb_program_text(self%h, c_loc(buf), room, need)
    if (st /= ZXV_OK) return
    s = repeat(' ', int(need))
    s = transfer(buf(1:need), s)
  end function

  !> SHA3-256 of the canonical text, as 64 hex digits: the program's identity.
  function tbp_digest(self) result(s)
    class(trebago_program), intent(in) :: self
    character(len=64) :: s
    integer(c_int8_t) :: dg(32)
    character(len=16), parameter :: hx = '0123456789abcdef'
    integer :: i, v
    s = ''
    if (.not. c_associated(self%h)) return
    if (tb_program_digest(self%h, dg) /= ZXV_OK) return
    do i = 1, 32
      v = iand(int(dg(i)), 255)
      s(2 * i - 1:2 * i) = hx(v / 16 + 1:v / 16 + 1) // hx(mod(v, 16) + 1:mod(v, 16) + 1)
    end do
  end function

  subroutine tbp_destroy(self)
    class(trebago_program), intent(inout) :: self
    if (c_associated(self%h)) call tb_program_destroy(self%h)
    self%h = c_null_ptr
    self%nb = 0
  end subroutine

  !> Copying a program re-parses its canonical text; bindings are not copied.
  subroutine tbp_assign(lhs, rhs)
    class(trebago_program), intent(inout) :: lhs
    class(trebago_program), intent(in) :: rhs
    character(len=:), allocatable :: t
    t = tbp_text(rhs)
    call tbp_destroy(lhs)
    lhs%limit = rhs%limit
    if (len(t) > 0) call tbp_parse(lhs, t)
  end subroutine

  impure elemental subroutine tbp_final(self)
    type(trebago_program), intent(inout) :: self
    call tbp_destroy(self)
  end subroutine

end module trebago
