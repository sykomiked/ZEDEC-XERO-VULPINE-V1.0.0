' Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
' SPDX-License-Identifier: Apache-2.0
' hfpdemo.bas - BASIC binding SKETCH for the ZEDEC legacy bridge.
' DOCUMENT ONLY: this illustrates how a BASIC with C FFI (FreeBASIC shown here)
' would declare and call the libzxlegacy C ABI. It is not built in this
' container. Declarations match bindings/zx_legacy_api.h.

#inclib "zxlegacy"

Declare Function zx_legacy_abi_version cdecl alias "zx_legacy_abi_version" () As Long
Declare Function zx_hfp64_to_ieee64 cdecl alias "zx_hfp64_to_ieee64" (ByVal hfp As ULongInt) As ULongInt
Declare Function zx_q850_to_sip_status cdecl alias "zx_q850_to_sip_status" (ByVal cause As UByte) As Long

Print "ZEDEC legacy ABI version: "; zx_legacy_abi_version()

Dim ieee As ULongInt
ieee = zx_hfp64_to_ieee64(&H4110000000000000ULL)  ' IBM HFP 1.0
Print "HFP 1.0 -> IEEE: "; Hex(ieee)               ' expect 3FF0000000000000

Print "Q.850 cause 17 -> SIP "; zx_q850_to_sip_status(17)  ' expect 486
