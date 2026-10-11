! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
!
! test_fatal.f90 - a poisoned context stops the program, even with stat=.
!
! `make fatal` runs this and requires: exit status 70 (ZXV_FATAL_EXIT),
! ZXV_E_POISONED on stderr, and no AFTER-STOP on stdout. A stat= argument
! hands back recoverable refusals; it cannot hand back a FATAL one, because
! nothing on that context can succeed any more and a caller that kept going
! would be computing with a ledger whose invariants are no longer known.
program test_fatal
  use, intrinsic :: iso_fortran_env, only: int32, int64
  use zxv
  implicit none
  type(context) :: ctx
  type(capability) :: root
  type(ledger) :: books
  integer(int32) :: a, b, st

  call ctx%create()
  call ctx%root_capability(root)
  call books%init(root)
  call books%enable_currency('NGN')
  call books%open_account('bank', 'NGN', ACCOUNT_ISSUER, a)
  call books%open_account('alice', 'NGN', ACCOUNT_HOLDER, b)
  call books%post(a, b, 100_int64, 'NGN', 'FUND-1', stat=st)
  if (st /= ZXV_OK) error stop 2

  call ctx%halt('test_fatal: deliberate halt')
  if (.not. ctx%is_poisoned()) error stop 3
  write (*, '(a)') 'BEFORE-STOP'
  flush (6)

  ! a recoverable refusal would come back in st; ZXV_E_POISONED stops here
  call books%post(a, b, 1_int64, 'NGN', 'PAY-1', stat=st)

  write (*, '(a,i0)') 'AFTER-STOP: post returned ', st
end program test_fatal
