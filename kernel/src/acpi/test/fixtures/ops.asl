/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ops.asl — one method per opcode family; test_aml_ops.c checks each result. */
DefinitionBlock ("", "DSDT", 2, "ZXV", "AMLOPS", 0x00000001)
{
    Name (GINT, 0x10)
    Name (GSTR, "hello")
    Name (GBUF, Buffer (4) {0x01, 0x02, 0x03, 0x04})
    Name (GPKG, Package (4) {1, "two", Buffer (2) {3, 3}, Package (2) {4, 5}})
    Name (CK41, 41)
    Name (RINT, 41)
    Name (RIN2, 0)
    Name (PKGM, Package (3) {0, 0, 0})
    Name (BUFM, Buffer (3) {0x11, 0, 0})
    Name (SBUF, Buffer (4) {})
    Name (SINT, 0)
    Name (SSTR, "")
    Name (CINT, 5)
    Name (FBUF, Buffer (8) {})
    Name (FBF2, Buffer (16) {})
    Name (PKGN, Package (1) {CK41})
    Name (FWD, 0)
    Mutex (MTX0, 0)
    Event (EVT0)
    Device (DEV0)
    {
        Name (_HID, "ZXV0001")
        Name (DSIB, 99)
        Method (UPRF, 0) { Return (^DSIB) }
    }
    Processor (CPU0, 1, 0x0000B010, 6) {}
    PowerResource (PWR0, 0, 0) {
        Method (_STA) { Return (1) }
        Method (_ON) {}
        Method (_OFF) {}
    }
    ThermalZone (TZ0) { Method (_TMP) { Return (3000) } }
    OperationRegion (REG0, SystemMemory, 0xFED40000, 0x10)
    Field (REG0, AnyAcc, NoLock, Preserve) { FLD0, 8 }
    CreateDWordField (FBF2, 0, BFLD)
    Alias (CK41, ALI0)

    /* module-level code (forward calls are tested with hand-built AML) */
    Method (MODL, 1) { Return (Arg0 * 7) }
    FWD = MODL (3)

    /* arithmetic */
    Method (T001) { Return (Add (0x7FFFFFFFFFFFFFFF, 2)) }
    Method (T002) { Return (Subtract (3, 5)) }
    Method (T003) { Return (Multiply (0x10000, 0x10000)) }
    Method (T004) { Divide (100, 7, Local0, Local1) Return ((Local0 * 1000) + Local1) }
    Method (T005) { Return (Mod (100, 7)) }
    Method (T006) { Return (ShiftLeft (1, 40)) }
    Method (T007) { Return (ShiftRight (0x8000000000000000, 63)) }
    Method (T008) { Local0 = 64 Return (ShiftLeft (1, Local0)) }
    Method (T009) { Return (And (0xF0F0, 0x0FF0)) }
    Method (T010) { Return (Or (0xF000, 0x000F)) }
    Method (T011) { Return (Xor (0xFF00, 0x0FF0)) }
    Method (T012) { Return (NAnd (0xF0, 0x3C)) }
    Method (T013) { Return (NOr (0xF0, 0x0F)) }
    Method (T014) { Return (Not (Zero)) }
    Method (T015) { Return (FindSetLeftBit (0x80)) }
    Method (T016) { Return (FindSetRightBit (0x80)) }
    Method (T017) { Local0 = Zero Return (FindSetLeftBit (Local0)) }
    Method (T018) { Return (FindSetRightBit (0x0A)) }
    Method (T019) { Local0 = 5 Increment (Local0) Increment (Local0) Decrement (Local0) Return (Local0) }
    Method (T020) { Return (Divide (9, 2)) }
    Method (T021) { Local0 = 0 Return (Divide (1, Local0)) }
    Method (T022) { Local0 = 0 Return (Mod (1, Local0)) }
    Method (T023) { Add (1, 2, Local0) Return (Local0) }
    Method (T024) { Local0 = 0 Subtract (Local0, 1, Local1) Return (Local1) }
    Method (T025) { Local0 = 0x123456789 Local1 = 0 Divide (Local0, 0x10, Local1, Local2) Return ((Local2 << 4) | Local1) }
    Method (T026) { Local0 = 0xFFFFFFFFFFFFFFFF Increment (Local0) Return (Local0) }
    Method (T027) { Local0 = 0 Decrement (Local0) Return (Local0) }
    Method (T028) { Return (FindSetLeftBit (0x8000000000000001)) }
    Method (T029) { Return (FindSetRightBit (0x8000000000000000)) }

    /* logic */
    Method (T030) { Return (LAnd (1, 2)) }
    Method (T031) { Return (LAnd (1, 0)) }
    Method (T032) { Return (LOr (0, 0)) }
    Method (T033) { Return (LOr (0, 5)) }
    Method (T034) { Return (LNot (0)) }
    Method (T035) { Return (LNot (7)) }
    Method (T036) { Return (LEqual (5, 5)) }
    Method (T037) { Return (LGreater (6, 5)) }
    Method (T038) { Return (LLess (6, 5)) }
    Method (T039) { Return (LNotEqual (5, 6)) }
    Method (T040) { Return (LLessEqual (5, 5)) }
    Method (T041) { Return (LGreaterEqual (4, 5)) }
    Method (T042) { Return (LEqual ("abc", "abc")) }
    Method (T043) { Return (LLess ("abc", "abd")) }
    Method (T044) { Return (LGreater ("abcd", "abc")) }
    Method (T045) { Return (LEqual (Buffer () {1, 2}, Buffer () {1, 2})) }
    Method (T046) { Return (LLess (Buffer () {1}, Buffer () {1, 0})) }
    Method (T047) { Local0 = "10" Return (LEqual (0x10, Local0)) }
    Method (T048, 1)
    {
        If (Arg0 > 5) { Return (1) }
        ElseIf (Arg0 > 2) { Return (2) }
        Else { Return (3) }
    }
    Method (T049) { Return (LGreater (5, 6)) }

    /* control flow */
    Method (T050) { Local0 = 0 Local1 = 1 While (Local1 <= 10) { Local0 += Local1 Local1++ } Return (Local0) }
    Method (T051) { Local0 = 0 While (One) { Local0++ If (Local0 == 5) { Break } } Return (Local0) }
    Method (T052)
    {
        Local0 = 0
        Local1 = 0
        While (Local1 < 10) {
            Local1++
            If ((Local1 & 1) == 0) { Continue }
            Local0 += Local1
        }
        Return (Local0)
    }
    Method (T053)
    {
        Local0 = 0
        Local2 = 0
        While (Local0 < 3) {
            Local0++
            Local1 = 0
            While (One) { Local1++ Local2++ If (Local1 >= 4) { Break } }
        }
        Return (Local2)
    }
    Method (T054, 1, Serialized)
    {
        Switch (ToInteger (Arg0)) {
            Case (1) { Return (10) }
            Case (Package () {2, 3}) { Return (20) }
            Default { Return (30) }
        }
        Return (0)
    }
    Method (FACT, 1, Serialized) { If (Arg0 <= 1) { Return (1) } Return (Arg0 * FACT (Arg0 - 1)) }
    Method (T055) { Return (FACT (10)) }
    Method (SUM7, 7) { Return (Arg0 + Arg1 + Arg2 + Arg3 + Arg4 + Arg5 + Arg6) }
    Method (T056) { Return (SUM7 (1, 2, 3, 4, 5, 6, 7)) }
    Method (NORT) { Local0 = 1 }
    Method (T057) { Return (MODL (6)) }
    Method (T058) { Local0 = 0 While (One) { Local0++ } Return (Local0) }
    Method (DEEP, 1) { Return (DEEP (Arg0 + 1)) }
    Method (T059) { Return (DEEP (0)) }

    /* strings, buffers, conversions */
    Method (T060) { Local0 = "hello" Return (SizeOf (Local0)) }
    Method (T061) { Return (SizeOf (GBUF)) }
    Method (T062) { Return (SizeOf (GPKG)) }
    Method (T063) { Return (Concatenate ("ab", "cd")) }
    Method (T064) { Return (Concatenate (Buffer () {1, 2}, Buffer () {3})) }
    Method (T065) { Return (Concatenate (1, 2)) }
    Method (T066) { Return (Concatenate ("x", 0x1F)) }
    Method (T067) { Return (ToHexString (0xAB)) }
    Method (T068) { Return (ToHexString (Buffer () {0xAB, 0x01})) }
    Method (T069) { Return (ToDecimalString (1234)) }
    Method (T070) { Return (ToDecimalString (Buffer () {1, 200, 34})) }
    Method (T071) { Return (ToInteger ("0x1F")) }
    Method (T072) { Return (ToInteger ("123")) }
    Method (T073) { Return (ToInteger (Buffer () {0x34, 0x12})) }
    Method (T074) { Return (ToBuffer ("AB")) }
    Method (T075) { Return (ToBuffer (0x0102)) }
    Method (T076) { Return (ToString (Buffer () {0x41, 0x42, 0, 0x43}, Ones)) }
    Method (T077) { Return (ToString (Buffer () {0x41, 0x42, 0x43}, 2)) }
    Method (T078) { Return (Mid ("hello", 1, 3)) }
    Method (T079) { Return (Mid (Buffer () {1, 2, 3, 4}, 2, 10)) }
    Method (T080) { Return (Mid ("abc", 5, 1)) }
    Method (T081) { Return (ToBCD (1234)) }
    Method (T082) { Return (FromBCD (0x9876)) }
    Method (T083)
    {
        Return (ObjectType (GINT) | (ObjectType (GSTR) << 8) | (ObjectType (GBUF) << 16) |
                (ObjectType (GPKG) << 24) | (ObjectType (DEV0) << 32) |
                (ObjectType (MTX0) << 40) | (ObjectType (EVT0) << 48) | (ObjectType (T001) << 56))
    }
    Method (T084)
    {
        Return (ObjectType (REG0) | (ObjectType (FLD0) << 8) | (ObjectType (BFLD) << 16) |
                (ObjectType (CPU0) << 24) | (ObjectType (TZ0) << 32) |
                (ObjectType (PWR0) << 40) | (ObjectType (Debug) << 48) | (ObjectType (Local0) << 56))
    }
    Method (T085) { Return (Concatenate ("a", Buffer () {1, 0xA2})) }
    Method (T086) { Local0 = "abc" Return (ToHexString (Local0)) }
    Method (T087) { Return (ToInteger (" 0x10zz")) }
    Method (T088) { Return (ToDecimalString ("str")) }
    Method (T089) { Return (ConcatenateResTemplate (
                ResourceTemplate () { IO (Decode16, 0x60, 0x60, 0, 1) },
                ResourceTemplate () { IO (Decode16, 0x64, 0x64, 0, 1) })) }

    /* packages, Index, references */
    Method (T090) { Return (DerefOf (Index (GPKG, 0))) }
    Method (T091) { Return (SizeOf (DerefOf (Index (GPKG, 1)))) }
    Method (T092) { Return (DerefOf (Index (DerefOf (Index (GPKG, 3)), 1))) }
    Method (T093) { Store (9, Index (PKGM, 2)) Return (DerefOf (Index (PKGM, 2))) }
    Method (T094) { Store (0x55, Index (BUFM, 1)) Return (DerefOf (Index (BUFM, 1)) | (DerefOf (Index (BUFM, 0)) << 8)) }
    Method (T095) { Local0 = Package () {1, 2, 3} Local0 [1] = 7 Return (DerefOf (Local0 [1]) + SizeOf (Local0)) }
    Method (T096) { Local0 = RefOf (RINT) Return (DerefOf (Local0) + 1) }
    Method (SETR, 1) { Arg0 = 77 }
    Method (T097) { SETR (RefOf (RIN2)) Return (RIN2) }
    Method (T098)
    {
        Local1 = 0
        If (CondRefOf (\_SB.NOPE)) { Local1 |= 1 }
        If (CondRefOf (\T001, Local0)) { Local1 |= 2 }
        If (ObjectType (Local0) == 8) { Local1 |= 4 }
        Return (Local1)
    }
    Method (T099) { Return (Match (Package () {1, 5, 10, 15}, MGE, 6, MLT, 12, 0)) }
    Method (T100) { Return (Match (Package () {1, 5, 10, 15}, MEQ, 99, MTR, 0, 0)) }
    Method (T101) { Return (Match (Package () {5, 5, 5}, MEQ, 5, MTR, 0, 1)) }
    Method (T102) { Return (DerefOf (Index (GPKG, 9))) }
    Method (T103, 1) { Local0 = Package (Arg0) {} Return (SizeOf (Local0)) }
    Method (T104) { Return (DerefOf (Index (PKGN, 0)) + 0) }
    Method (T105) { Local0 = Index (GPKG, 2) Return (DerefOf (Index (DerefOf (Local0), 1))) }
    Method (T106) { Local0 = Buffer (3) {7, 8, 9} Local1 = Index (Local0, 2) Return (DerefOf (Local1)) }
    Method (T107) { Return (Match (Package () {"a", "bb", "c"}, MEQ, "bb", MTR, 0, 0)) }
    Method (T108) { Return (Match (Package () {1, 2, 3}, MLE, 2, MGT, 1, 0)) }
    Method (T109) { Local0 = Package () {1} Return (DerefOf (Index (Local0, 1))) }

    /* stores and implicit conversion */
    Method (T110) { Store (0x11223344AABB, SBUF) Return ((SizeOf (SBUF) << 32) | ToInteger (SBUF)) }
    Method (T111) { Store ("1A", SINT) Return (SINT) }
    Method (T112) { Store (0x41, SSTR) Return (SizeOf (SSTR)) }
    Method (T113) { CopyObject ("str", CINT) Return (ObjectType (CINT)) }
    Method (T114)
    {
        CreateDWordField (FBUF, 2, FDW)
        CreateBitField (FBUF, 0, FB0)
        CreateQWordField (FBUF, 0, FQW)
        FDW = 0x12345678
        FB0 = 1
        Return (FQW)
    }
    Method (T115) { Local0 = Buffer () {0xA5, 0x3C} CreateField (Local0, 4, 8, LNB) Return (LNB) }
    Method (OSCT, 4)
    {
        CreateDWordField (Arg3, 0, CDW1)
        CreateDWordField (Arg3, 4, CDW2)
        CDW1 |= 4
        CDW2 = 0x1F
        Return (Arg3)
    }
    Method (T116)
    {
        Local0 = OSCT (ToUUID ("33DB4D5B-1FF7-401C-9657-7441C03DD766"), 1, 2, Buffer (8) {1, 0, 0, 0, 0, 0, 0, 0})
        Return (ToInteger (Local0))
    }
    Method (T117) { CreateField (FBF2, 0, 96, F96) Local0 = F96 Return (SizeOf (Local0)) }
    Method (T118) { Local0 = Buffer (2) {} CreateWordField (Local0, 1, WOVR) Return (WOVR) }
    Method (T119) { CreateByteField (FBUF, 1, FBY) CreateWordField (FBUF, 6, FWD2) FBY = 0x1FF FWD2 = 0xBEEF Return (ToInteger (FBUF)) }

    /* mutex, event */
    Method (T120) { Local0 = Acquire (MTX0, 0xFFFF) Release (MTX0) Return (Local0 + 5) }
    Method (T121) { Release (MTX0) Return (1) }
    Method (T122)
    {
        Signal (EVT0)
        Signal (EVT0)
        Local0 = Wait (EVT0, 10)
        Local1 = Wait (EVT0, 10)
        Local2 = Wait (EVT0, 10)
        Reset (EVT0)
        Local3 = 0
        If (Local0 == 0) { Local3 |= 1 }
        If (Local1 == 0) { Local3 |= 2 }
        If (Local2 != 0) { Local3 |= 4 }
        Return (Local3)
    }
    Method (T123) { Local0 = Acquire (\_GL, 0xFFFF) Release (\_GL) Return (Local0) }
    Method (T124) { Acquire (MTX0, 0xFFFF) Acquire (MTX0, 0xFFFF) Release (MTX0) Release (MTX0) Release (MTX0) Return (1) }

    /* notify, sleep, timer, debug, fatal */
    Method (T130) { Notify (DEV0, 0x80) Notify (\_SB, 0x81) Return (2) }
    Method (T131) { Sleep (10) Stall (50) Return (1) }
    Method (T132) { Local0 = Timer Sleep (1) Local1 = Timer Return (Local1 - Local0) }
    Method (T133) { Debug = "msg" Debug = 5 Return (1) }
    Method (T134) { Fatal (1, 0x1234, 0) Return (1) }
    Method (T135) { Return (Revision) }
    Method (T136) { Sleep (4000) Return (1) }
    Method (T137) { Notify (CPU0, 1) Return (1) }

    /* names and scopes */
    Method (T140) { Return (ALI0) }
    Method (T141, 0, Serialized) { Name (LOC1, 5) LOC1 += 1 Return (LOC1) }
    Method (T142, 0, Serialized) { Device (TMPD) { Name (_ADR, 0) Name (INNR, 3) } Return (\T142.TMPD.INNR) }
    Method (T143) { Local0 = 0 If (_OSI ("Windows 2015")) { Local0 |= 1 } If (_OSI ("Linux")) { Local0 |= 2 } Return (Local0) }
    Method (T144) { Return ((\_REV << 8) | SizeOf (\_OS)) }
    Method (T145) { Return (FWD) }
    Method (T146) { Return (\DEV0.UPRF ()) }
    Method (T147) { Return (\_SB.ZXSB) }
    Method (T148) { Local0 = "CK41" Return (DerefOf (Local0)) }
    Method (T149) { Return (ObjectType (\_SB)) }
    Scope (\_SB) { Name (ZXSB, 0x5B) }
}
