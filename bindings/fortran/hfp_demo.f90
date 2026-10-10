! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
! hfp_demo.f90 — calls the ZEDEC bridge to convert an IBM hexadecimal floating
! point bit pattern (as a legacy mainframe Fortran program would hold it) into
! an IEEE 754 double bit pattern, in integer arithmetic. Exits non-zero on any
! mismatch so a CI harness can gate on it.
program hfp_demo
   use zx_legacy_bind
   use, intrinsic :: iso_c_binding
   implicit none
   integer(c_int64_t) :: hfp_one, ieee_one, back
   integer(c_int32_t) :: ver
   integer :: status

   status = 0
   ver = zx_legacy_abi_version()
   print '(A,I0)', 'ZEDEC legacy ABI version: ', ver

   ! IBM HFP double 1.0 = 0x4110000000000000 ; IEEE double 1.0 = 0x3FF0000000000000
   hfp_one = int(z'4110000000000000', c_int64_t)
   ieee_one = zx_hfp64_to_ieee64(hfp_one)
   print '(A,Z16)', 'HFP 1.0 -> IEEE: ', ieee_one
   if (ieee_one /= int(z'3FF0000000000000', c_int64_t)) status = 1

   back = zx_ieee64_to_hfp64(ieee_one)
   print '(A,Z16)', 'IEEE 1.0 -> HFP: ', back
   if (back /= hfp_one) status = 1

   if (status == 0) then
      print '(A)', 'Fortran binding round trip OK'
   else
      print '(A)', 'Fortran binding MISMATCH'
   end if
   call exit(status)
end program hfp_demo
