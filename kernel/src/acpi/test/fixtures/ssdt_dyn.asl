/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ssdt_dyn.asl — a table loaded at run time with Load() (regions.asl R026),
 * the way processor SSDTs are loaded from _PDC/_OSC on real machines. */
DefinitionBlock ("", "SSDT", 2, "ZXV", "DYNLOAD", 0x00000001)
{
    External (\QHIT, IntObj)
    Name (\SSXV, 0x5A5A)
    Method (\SSXM, 1) { Return (Arg0 + \SSXV) }
}
