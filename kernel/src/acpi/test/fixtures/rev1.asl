/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* rev1.asl — a revision 1 DSDT: integers are 32 bits wide. */
DefinitionBlock ("", "DSDT", 1, "ZXV", "REV1", 0x00000001)
{
    Name (BIGV, 0xFFFFFFFF)
    Method (W001) { Return (BIGV + 1) }
    Method (W002) { Return (Ones) }
    Method (W003) { Return (~0) }
    Method (W004) { Local0 = 0x80000000 Return (Local0 << 1) }
    Method (W005) { Return (0 - 1) }
    Method (W006) { Local0 = Buffer () { 1, 2, 3, 4, 5, 6, 7, 8 } Return (ToInteger (Local0)) }
    Method (W007) { Local0 = ToHexString (0x12) Return (SizeOf (Local0)) }
    Method (W008) { Return (FindSetLeftBit (0x80000000)) }
    Method (W009) { Local0 = "0x123456789" Return (ToInteger (Local0)) }
    Method (W010) { Return (Revision > 0) }
}
