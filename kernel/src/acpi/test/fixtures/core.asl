/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* core.asl — one wrapper method per operator, called from test_aml_core.c
 * with many argument values (and loaded a second time as a revision 1 table,
 * 32-bit integers), plus the edge cases of the evaluator core: budgets,
 * references, stores into typed names, buffer fields, notify queue. */
DefinitionBlock ("", "DSDT", 2, "ZXV", "AMLCORE", 0x00000001)
{
    /* operators: Arg0 op Arg1 */
    Method (OADD, 2) { Return (Add (Arg0, Arg1)) }
    Method (OSUB, 2) { Return (Subtract (Arg0, Arg1)) }
    Method (OMUL, 2) { Return (Multiply (Arg0, Arg1)) }
    Method (OSHL, 2) { Return (ShiftLeft (Arg0, Arg1)) }
    Method (OSHR, 2) { Return (ShiftRight (Arg0, Arg1)) }
    Method (OAND, 2) { Return (And (Arg0, Arg1)) }
    Method (ONAN, 2) { Return (NAnd (Arg0, Arg1)) }
    Method (OOR_, 2) { Return (Or (Arg0, Arg1)) }
    Method (ONOR, 2) { Return (NOr (Arg0, Arg1)) }
    Method (OXOR, 2) { Return (Xor (Arg0, Arg1)) }
    Method (OMOD, 2) { Return (Mod (Arg0, Arg1)) }
    Method (ODIV, 2) { Return (Divide (Arg0, Arg1)) }
    Method (ODVR, 2) { Divide (Arg0, Arg1, Local0, Local1) Return (Local0) }
    Method (ODVQ, 2) { Divide (Arg0, Arg1, Local0, Local1) Return (Local1) }
    Method (ONOT, 1) { Return (Not (Arg0)) }
    Method (OFSL, 1) { Return (FindSetLeftBit (Arg0)) }
    Method (OFSR, 1) { Return (FindSetRightBit (Arg0)) }
    Method (OLAN, 2) { Return (LAnd (Arg0, Arg1)) }
    Method (OLOR, 2) { Return (LOr (Arg0, Arg1)) }
    Method (OLNT, 1) { Return (LNot (Arg0)) }
    Method (OLEQ, 2) { Return (LEqual (Arg0, Arg1)) }
    Method (OLGT, 2) { Return (LGreater (Arg0, Arg1)) }
    Method (OLLT, 2) { Return (LLess (Arg0, Arg1)) }
    Method (OCAT, 2) { Return (Concatenate (Arg0, Arg1)) }
    Method (OCRT, 2) { Return (ConcatenateResTemplate (Arg0, Arg1)) }
    Method (OTBF, 1) { Return (ToBuffer (Arg0)) }
    Method (OTDS, 1) { Return (ToDecimalString (Arg0)) }
    Method (OTHS, 1) { Return (ToHexString (Arg0)) }
    Method (OTIN, 1) { Return (ToInteger (Arg0)) }
    Method (OTST, 2) { Return (ToString (Arg0, Arg1)) }
    Method (OMID, 3) { Return (Mid (Arg0, Arg1, Arg2)) }
    Method (OFBC, 1) { Return (FromBCD (Arg0)) }
    Method (OTBC, 1) { Return (ToBCD (Arg0)) }
    Method (OSIZ, 1) { Return (SizeOf (Arg0)) }
    Method (OTYP, 1) { Return (ObjectType (Arg0)) }
    Method (OINC, 1) { Increment (Arg0) Return (Arg0) }
    Method (ODEC, 1) { Decrement (Arg0) Return (Arg0) }
    Method (OIDX, 2) { Return (DerefOf (Index (Arg0, Arg1))) }
    Method (OITY, 2) { Return (ObjectType (Index (Arg0, Arg1))) }
    Method (OMEQ, 3) { Return (Match (Arg0, MEQ, Arg1, MTR, 0, Arg2)) }
    Method (OMLE, 3) { Return (Match (Arg0, MLE, Arg1, MTR, 0, Arg2)) }
    Method (OMLT, 3) { Return (Match (Arg0, MLT, Arg1, MTR, 0, Arg2)) }
    Method (OMGE, 3) { Return (Match (Arg0, MGE, Arg1, MTR, 0, Arg2)) }
    Method (OMGT, 3) { Return (Match (Arg0, MGT, Arg1, MTR, 0, Arg2)) }
    Method (OMTR, 3) { Return (Match (Arg0, MTR, Arg1, MEQ, Arg1, Arg2)) }
    Method (OSTR, 2) { Store (Arg0, Arg1) Return (Arg1) }
    Method (OCPY, 1) { CopyObject (Arg0, Local0) Return (Local0) }
    Method (OBUF, 1) { Return (Buffer (Arg0) {1, 2, 3}) }
    Method (OPKG, 1) { Return (Package (Arg0) {1, 2, 3}) }

    /* stores into typed names keep the type of the target */
    Name (NINT, 0)
    Name (NSTR, "zz")
    Name (NBUF, Buffer (3) {9, 9, 9})
    Name (NBF0, Buffer (0) {})
    Name (NPKG, Package (2) {1, 2})
    Method (SINT, 1) { Store (Arg0, NINT) Return (NINT) }
    Method (SSTR, 1) { Store (Arg0, NSTR) Return (NSTR) }
    Method (SBUF, 1) { Store (Arg0, NBUF) Return (NBUF) }
    Method (SBF0, 1) { Store (Arg0, NBF0) Return (NBF0) }
    Method (SPKG, 1) { Store (Arg0, NPKG) Return (NPKG) }
    Method (MTH0) { Return (1) }
    Method (SMTH, 1) { Store (Arg0, MTH0) Return (1) }

    /* buffer fields */
    Name (FB16, Buffer (16) {})
    CreateField (FB16, 4, 72, F72)
    CreateField (FB16, 3, 64, F64)
    CreateField (FB16, 0, 128, F128)
    CreateBitField (FB16, 127, FBIT)
    Method (BFW7, 1) { F72 = Arg0 Return (FB16) }
    Method (BFW6, 1) { F64 = Arg0 Return (FB16) }
    Method (BFR7) { Return (F72) }
    Method (BFR6) { Return (F64) }
    Method (BFCL) { F128 = Zero Return (FB16) }
    Method (BFBT) { FBIT = One Return (FB16) }
    Method (BFE1) { Local0 = Buffer (4) {} CreateField (Local0, 0, 33, BIG1) Return (1) }
    Method (BFE2) { Local0 = Buffer (4) {} CreateField (Local0, 32, 1, BIG2) Return (1) }
    Method (BFE3) { Local0 = Buffer (4) {} CreateField (Local0, 31, 1, BIG3) BIG3 = 1 Return (Local0) }
    Method (BFE4) { Local0 = Buffer (4) {} CreateField (Local0, 0, 0, BIG4) Return (1) }
    Method (BFE5) { Local0 = Buffer (4) {} CreateDWordField (Local0, 0, BIG5) BIG5 = 0x89ABCDEF Return (Local0) }
    Method (BFE6) { Local0 = Buffer (8) {} CreateQWordField (Local0, 1, BIG6) Return (1) }
    Method (BFE7) { Local0 = Buffer (8) {} CreateQWordField (Local0, 0, BIG7) BIG7 = 0x0102030405060708 Return (Local0) }
    Method (BFE8) { Local0 = Buffer (2) {} CreateByteField (Local0, 1, BIG8) BIG8 = 0x1AB Return (Local0) }
    Method (BFE9) { Local0 = Buffer (2) {} CreateByteField (Local0, 2, BIG9) Return (1) }
    Method (BFEA) { Local0 = 5 CreateByteField (Local0, 0, BIGA) Return (1) }
    Method (BFEB) { Local0 = Buffer (4) {0xFF, 0xFF, 0xFF, 0xFF} CreateField (Local0, 4, 16, BIGB) BIGB = "A" Return (Local0) }
    Method (BFEC) { Local0 = Buffer (4) {0xFF, 0xFF, 0xFF, 0xFF} CreateField (Local0, 8, 16, BIGC) BIGC = Buffer (1) {0x12} Return (Local0) }

    /* control flow budgets */
    Method (LOOP, 1) { Local0 = 0 While (Local0 < Arg0) { Local0++ } Return (Local0) }
    Method (SLPU, 1) { Stall (Arg0) Return (1) }
    Method (SLPM, 1) { Sleep (Arg0) Return (1) }
    Method (TIMR) { Return (Timer) }

    /* references */
    Name (RVAL, 7)
    Name (RPKG, Package (3) {1, "s", Package (1) {2}})
    Name (RBUF, Buffer (3) {4, 5, 6})
    Name (RSTR, "xyz")
    Method (REF1) { Local0 = RefOf (RVAL) Return (ObjectType (Local0)) }
    Method (REF2) { Local0 = Index (RPKG, 1) Return (ObjectType (Local0)) }
    Method (REF3) { Local0 = Index (RBUF, 1) Return (ObjectType (Local0)) }
    Method (REF4) { Local0 = Index (RSTR, 2) Return (DerefOf (Local0)) }
    Method (REF5) { Index (RSTR, 0) = 0x41 Return (RSTR) }
    Method (REF6) { Index (RBUF, 2) = 0x1FF Return (RBUF) }
    Method (REF7) { Return (Index (RBUF, 3)) }
    Method (REF8) { Local0 = RefOf (RVAL) Return (SizeOf (Local0)) }
    Method (REF9) { Local0 = RefOf (RSTR) Return (SizeOf (Local0)) }
    Method (REFA) { Local0 = RefOf (RVAL) Return (Local0 + 1) }
    Method (REFB) { Local0 = RefOf (RVAL) Return (Concatenate (Local0, 2)) }
    Method (REFC) { Local0 = RefOf (RVAL) Return (ToBuffer (Local0)) }
    Method (REFD) { Local0 = Index (RPKG, 2) Return (ObjectType (Local0)) }
    Method (REFE) { Local0 = 9 Local1 = RefOf (Local0) Return (Local1) }
    Method (REFF) { Local0 = 5 Return (DerefOf (Local0)) }
    Method (REFG) { Local0 = RefOf (RVAL) Return (LEqual (Local0, 7)) }
    Method (REFH) { Local0 = "RVAL" Return (ObjectType (Local0)) }
    Method (REFI) { Local0 = Index (RPKG, 0) Local0 = 3 Return (DerefOf (Index (RPKG, 0))) }
    Method (WREF, 1) { Arg0 = 5 }
    Method (REFJ) { Local0 = 1 WREF (RefOf (Local0)) Return (Local0) }
    Method (REFK) { CopyObject (5, Local0) Local1 = RefOf (Local0) CopyObject (Local1, Local2) Return (DerefOf (Local2)) }
    Method (REFL) { Return (RefOf (Local7)) }
    Name (RVL2, 0)
    Name (RVL3, 0)
    Name (HSTR, "1F")
    Method (REFM) { Return (Index (RBUF, 1)) }
    Method (REFN) { Return (Index (Package () {1, 2}, 1)) }
    Method (REFO) { Local0 = RefOf (RVL2) Store (5, DerefOf (Local0)) Return (RVL2) }
    Method (REFP) { Local0 = "RVL3" Store (6, DerefOf (Local0)) Return (RVL3) }
    Method (RFB1, 1) { Return (Arg0) }
    Method (RFA1) { Local0 = 3 Local1 = RFB1 (RefOf (Local0)) Local0 = 4 Return (DerefOf (Local1)) }
    Method (RFA2) { Local0 = 3 Return (RFB1 (RefOf (Local0))) }
    Method (RFA3, 1) { Return (RefOf (Arg0)) }
    Method (RDEP, 1) { If (Arg0) { Return (RDEP (Arg0 - 1) + 1) } Return (0) }
    Method (QWRD) { Return (0x1122334455667788) }

    /* package construction */
    Device (PDEV) { Name (_ADR, 0) }
    External (NOPE, IntObj)
    Method (PKG2) { Return (Package (3) {PDEV, NOPE, RVAL}) }
    Method (PKG3) { Local0 = Package (2) {RVAL} Return (ObjectType (DerefOf (Index (Local0, 1)))) }

    /* names */
    Name (\_SB.SBV0, 0x5B)
    Alias (\_SB.SBV0, AL01)
    Alias (AL01, AL02)
    Method (NAM1) { Return (AL02) }
    Method (NAM2) { Return (ObjectType (\_SB)) }

    /* notify queue */
    Device (NDEV) { Name (_ADR, 1) }
    Method (NTF1, 1) { Notify (NDEV, Arg0) }
    Method (NTF3, 0, Serialized) { Device (TMPN) { Name (_ADR, 2) } Notify (TMPN, 4) Notify (NDEV, 5) }

    /* mutex and events */
    Mutex (MX15, 15)
    Event (EV00)
    Method (MXA1) { Local0 = Acquire (MX15, 0) Local1 = Acquire (MX15, 0) Release (MX15) Release (MX15) Return (Local0 | Local1) }
    Method (EVW1) { Return (Wait (EV00, 0)) }
    Method (EVS1) { Signal (EV00) Signal (EV00) Signal (EV00) Reset (EV00) Return (Wait (EV00, 0)) }

    /* debug, unload, fatal */
    Method (DBG1) { Debug = Buffer (3) {} Debug = Package (2) {} Debug = RefOf (RVAL) Return (1) }
    Method (FTL1) { Fatal (0xAB, 0xDEADBEEF, 1) }
    Method (CRF1) { Local0 = 3 If (CondRefOf (NOPE, Local0)) { Return (0) } Return (Local0) }
    Method (CRF2) { CondRefOf (RVAL, Local0) Return (DerefOf (Local0)) }

    /* stale references: a temporary node, a returned frame, a reused frame */
    Method (SRT1) { Name (TMPV, 4) Return (RefOf (TMPV)) }
    Method (SRT2) { Local0 = SRT1 () Return (DerefOf (Local0)) }
    Method (SRL1) { Local0 = 5 Local1 = Package (1) {0} Store (RefOf (Local0), Index (Local1, 0)) Return (Local1) }
    Method (SRL2) { Local0 = SRL1 () Return (DerefOf (DerefOf (Index (Local0, 0)))) }
    Method (SRL3, 1) { Local0 = 9 Return (DerefOf (Arg0)) }
    Method (SRL4) { Local0 = SRL1 () Return (SRL3 (DerefOf (Index (Local0, 0)))) }
    Method (SRA6, 7) { Return (DerefOf (RefOf (Arg6))) }
    Method (SRL7) { Local7 = 0x17 Return (DerefOf (RefOf (Local7))) }
    /* reference chains: 8, 9 and 10 references deep */
    Name (RCN1, 0)
    Name (RCN2, 0)
    Method (RCH, 1) {
        Local0 = RefOf (RVAL)
        Local1 = RefOf (Local0)
        Local2 = RefOf (Local1)
        Local3 = RefOf (Local2)
        Local4 = RefOf (Local3)
        Local5 = RefOf (Local4)
        Local6 = RefOf (Local5)
        Local7 = RefOf (Local6)
        CopyObject (RefOf (Local7), RCN1)
        CopyObject (RefOf (RCN1), RCN2)
        If (Arg0 == 8) { Return (Local7 + 0) }
        If (Arg0 == 9) { Return (RCN1 + 0) }
        Return (RCN2 + 0)
    }
    Method (RCHS) { Return (DerefOf (RCN1)) }
    /* an indexed reference into a string that shrank or changed type */
    Name (RSH1, "abc")
    Method (RSB1) { CopyObject ("abc", RSH1) Local0 = Index (RSH1, 2) CopyObject ("ab", RSH1) Return (DerefOf (Local0)) }
    Method (RSB2) { CopyObject ("abc", RSH1) Local0 = Index (RSH1, 2) CopyObject ("ab", RSH1) Store (0x41, DerefOf (Local0)) Return (RSH1) }
    Method (RSB3) { CopyObject ("abc", RSH1) Local0 = Index (RSH1, 0) CopyObject (RefOf (RVAL), RSH1) Return (DerefOf (Local0)) }
    Method (RSB4) { CopyObject ("abc", RSH1) Local0 = Index (RSH1, 0) CopyObject (RefOf (RVAL), RSH1) Store (1, DerefOf (Local0)) Return (1) }
    Method (RSB5) { Store (Package () {1}, Index (RBUF, 0)) }
    Method (RSB6) { CopyObject ("abc", RSH1) Local0 = Index (RSH1, 1) Store (0x5A, DerefOf (Local0)) Return (RSH1) }
    /* objects read through a reference */
    Method (RNM1) { Return (DerefOf (RefOf (MTH0))) }
    Method (RND1) { Local0 = DerefOf (RefOf (NDEV)) Return (ObjectType (Local0)) }
    Method (RNF1) { F128 = Zero Local0 = RefOf (F64) Return (DerefOf (Local0)) }
    Method (RNF2, 1) { F128 = Zero Local0 = RefOf (F64) Store (Arg0, DerefOf (Local0)) Return (FB16) }
    /* buffer fields over the end of an integer, written from a reference */
    Name (BFX1, Buffer (16) {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x13, 0x0C, 0x30, 0xC0, 0x11, 0x22, 0x44, 0x88})
    CreateField (BFX1, 3, 65, BF65)
    CreateField (BFX1, 5, 33, BF33)
    Method (BFS1) { Return (BF65) }
    Method (BFS2) { Return (BF33) }
    Method (BFS3) { BF33 = RefOf (RVAL) Return (BFX1) }
    /* a buffer field whose buffer shrank, then became an integer */
    Name (BFX2, Buffer (4) {})
    CreateBitField (BFX2, 16, BFB2)
    Method (BFS4) { CopyObject (Buffer (2) {1, 2}, BFX2) Return (BFB2) }
    Method (BFS5) { CopyObject (Buffer (2) {1, 2}, BFX2) BFB2 = 1 Return (BFX2) }
    Method (BFS6) { CopyObject (5, BFX2) Return (BFB2) }
    Method (BFS7) { CopyObject (5, BFX2) BFB2 = 1 Return (BFX2) }
    Method (BFS8) { CopyObject (Buffer (3) {}, BFX2) BFB2 = 1 Return (BFX2) }
    /* Debug output of each kind */
    Method (DBG2) { Debug = 0x1F Debug = "hi" Debug = 12 Return (0) }
    /* package elements named by a name: data is read, other objects referenced */
    Method (PKT1) { Return (Package () {RVAL, RSTR, RBUF, RPKG, F64, MTH0, NDEV, NOPE}) }
    Name (PKLD, Package () {F64, RVAL})
    /* ObjectType through a stale reference and an index past a shrunk package */
    Method (SRT3) { Local0 = SRT1 () Return (ObjectType (Local0)) }
    Name (PKX1, Package (3) {1, 2, 3})
    Method (OTX1) { CopyObject (Package (3) {1, 2, 3}, PKX1) Local0 = Index (PKX1, 2) CopyObject (Package (1) {1}, PKX1) Return (ObjectType (Local0)) }
    /* If with Else, both ways */
    Method (IFE1, 1) { If (Arg0) { Local0 = 1 } Else { Local0 = 2 } Return (Local0) }
    Method (IFE2, 1) { If (Arg0) { Local0 = 1 } Else { Local0 = NOPE } Return (Local0) }
}
