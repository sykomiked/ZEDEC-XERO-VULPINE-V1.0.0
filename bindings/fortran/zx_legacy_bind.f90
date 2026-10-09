! Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
! SPDX-License-Identifier: Apache-2.0
! zx_legacy_bind.f90 — Fortran 2003 ISO_C_BINDING interfaces to the ZEDEC
! legacy bridge C ABI (libzxlegacy). Legacy Fortran numerics code links this
! module to reach the bridge without a C shim of its own.
module zx_legacy_bind
   use, intrinsic :: iso_c_binding
   implicit none

   integer(c_int32_t), parameter :: ZX_OK = 0

   interface
      function zx_legacy_abi_version() bind(C, name="zx_legacy_abi_version") result(v)
         import :: c_int32_t
         integer(c_int32_t) :: v
      end function

      function zx_hfp64_to_ieee64(hfp) bind(C, name="zx_hfp64_to_ieee64") result(r)
         import :: c_int64_t
         integer(c_int64_t), value :: hfp
         integer(c_int64_t) :: r
      end function

      function zx_ieee64_to_hfp64(ieee) bind(C, name="zx_ieee64_to_hfp64") result(r)
         import :: c_int64_t
         integer(c_int64_t), value :: ieee
         integer(c_int64_t) :: r
      end function

      function zx_comp3_decode(inbuf, nbytes, outval) bind(C, name="zx_comp3_decode") result(rc)
         import :: c_int32_t, c_int64_t, c_int8_t
         integer(c_int8_t), intent(in) :: inbuf(*)
         integer(c_int32_t), value :: nbytes
         integer(c_int64_t), intent(out) :: outval
         integer(c_int32_t) :: rc
      end function

      function zx_ebcdic_to_utf8(cp, inbuf, n, outbuf, cap) &
            bind(C, name="zx_ebcdic_to_utf8") result(rc)
         import :: c_int32_t, c_int8_t
         integer(c_int32_t), value :: cp, n, cap
         integer(c_int8_t), intent(in) :: inbuf(*)
         integer(c_int8_t), intent(out) :: outbuf(*)
         integer(c_int32_t) :: rc
      end function
   end interface
end module zx_legacy_bind
