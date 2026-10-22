/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* regions.asl — operation regions and fields in every address space the
 * interpreter serves; test_aml_regions.c puts fake hardware behind them. */
DefinitionBlock ("", "DSDT", 2, "ZXV", "REGIONS", 0x00000001)
{
    Name (QHIT, 0)
    Name (DADR, 0xFED40080)
    Name (DDBH, 0)
    Name (SLEN, 2)
    External (\SSXV, IntObj)

    OperationRegion (MEM0, SystemMemory, 0xFED40000, 0x100)
    Field (MEM0, AnyAcc, NoLock, Preserve) {
        MB00, 8, MB01, 8, MW02, 16, MD04, 32, MQ08, 64,
        Offset (0x10), MBT0, 1, MBT1, 3, , 4, MBIG, 128,
        Offset (0x30), MX30, 12, MX31, 12
    }
    Field (MEM0, ByteAcc, NoLock, WriteAsOnes) { Offset (0x40), WO1, 4 }
    Field (MEM0, ByteAcc, NoLock, WriteAsZeros) { Offset (0x41), WZ1, 4 }
    Field (MEM0, DWordAcc, NoLock, Preserve) { Offset (0x44), , 8, PD1, 8 }
    Field (MEM0, QWordAcc, NoLock, Preserve) { Offset (0x48), QW1, 64 }
    Field (MEM0, AnyAcc, NoLock, Preserve) { Offset (0x50), AccessAs (ByteAcc), AB1, 16 }
    Field (MEM0, ByteAcc, Lock, Preserve) { Offset (0x60), LKF, 8 }
    Field (MEM0, WordAcc, NoLock, WriteAsZeros) { Offset (0x62), , 4, WZW, 4 }
    Field (MEM0, AnyAcc, NoLock, Preserve) { Offset (0xFF), EDG0, 8 }
    OperationRegion (SMAL, SystemMemory, 0xFED40100, SLEN)
    Field (SMAL, ByteAcc, NoLock, Preserve) { Offset (1), OVR, 16 }
    OperationRegion (DEFR, SystemMemory, DADR, 0x10)
    Field (DEFR, AnyAcc, NoLock, Preserve) { DF0, 32 }

    OperationRegion (IO0, SystemIO, 0x400, 0x10)
    Field (IO0, WordAcc, NoLock, Preserve) { IOW0, 16, IOB2, 8 }
    Field (IO0, DWordAcc, NoLock, Preserve) { Offset (8), IOD8, 32 }
    Field (IO0, ByteAcc, NoLock, Preserve) { Offset (12), IOBT, 1 }
    Field (IO0, QWordAcc, NoLock, Preserve) { Offset (8), IOQ8, 64 }

    Device (PCI0) {
        Name (_HID, EisaId ("PNP0A08"))
        Name (_CID, EisaId ("PNP0A03"))
        Name (_BBN, 0)
        Name (_SEG, 0)
        Name (_ADR, 0)
        Device (LPC0) {
            Name (_ADR, 0x001F0000)
            Name (REGD, 0)
            Method (_REG, 2) { If (Arg0 == 2) { REGD = Arg1 } }
            OperationRegion (LPCR, PCI_Config, 0x40, 0x10)
            Field (LPCR, DWordAcc, NoLock, Preserve) { PMBA, 32, PMB2, 16 }
        }
        Device (BR1) {
            Name (_ADR, 0x001C0002)
            Device (DEVX) {
                Name (_ADR, 0x00000001)
                OperationRegion (XCFG, PCI_Config, 0, 4)
                Field (XCFG, AnyAcc, NoLock, Preserve) { VID, 16, DID, 16 }
            }
        }
    }
    Device (PCI1) {
        Name (_HID, "PNP0A03")
        Method (_BBN) { Return (0x80) }
        Device (DEVY) {
            Name (_ADR, 0x00030000)
            OperationRegion (YCFG, PCI_Config, 0, 0x100)
            Field (YCFG, ByteAcc, NoLock, Preserve) { YV0, 8 }
        }
    }

    Device (EC0) {
        Name (_HID, EisaId ("PNP0C09"))
        Name (_GPE, 0x17)
        Name (_CRS, ResourceTemplate () {
            IO (Decode16, 0x62, 0x62, 0, 1)
            IO (Decode16, 0x66, 0x66, 0, 1)
        })
        Name (ECOK, 0)
        Method (_REG, 2) { If (Arg0 == 3) { ECOK = Arg1 } }
        OperationRegion (ECOR, EmbeddedControl, 0, 0xFF)
        Field (ECOR, ByteAcc, Lock, Preserve) { Offset (0x10), BCAP, 16, BST1, 8, Offset (0x20), TMP0, 8 }
        Method (_Q42) { QHIT = 0x42 }
    }

    OperationRegion (CMS0, SystemCMOS, 0, 0x100)
    Field (CMS0, ByteAcc, NoLock, Preserve) { Offset (0x32), CENT, 8, Offset (0x90), HIC, 8 }

    OperationRegion (IDXR, SystemIO, 0x500, 2)
    Field (IDXR, ByteAcc, NoLock, Preserve) { IDX, 8, DAT, 8 }
    IndexField (IDX, DAT, ByteAcc, NoLock, Preserve) { Offset (0x10), IF10, 8, IF11, 16 }

    OperationRegion (BNKR, SystemIO, 0x600, 1)
    Field (BNKR, ByteAcc, NoLock, Preserve) { BSEL, 8 }
    OperationRegion (BDAT, SystemIO, 0x610, 1)
    BankField (BDAT, BSEL, 1, ByteAcc, NoLock, Preserve) { BF1, 8 }
    BankField (BDAT, BSEL, 2, ByteAcc, NoLock, Preserve) { BF2, 8 }

    OperationRegion (OEMR, 0x80, 0, 0x10)
    Field (OEMR, ByteAcc, NoLock, Preserve) { OEMF, 8 }

    DataTableRegion (DREG, "DSDT", "", "")
    Field (DREG, AnyAcc, NoLock, Preserve) { DSIG, 32 }

    OperationRegion (SSDR, SystemMemory, 0xFED41000, 0x400)
    Field (SSDR, AnyAcc, NoLock, Preserve) { SSG0, 32 }

    Method (R001) {
        MB00 = 0x11
        MB01 = 0x22
        MW02 = 0x4433
        MD04 = 0x88776655
        MQ08 = 0x0123456789ABCDEF
        Return (MQ08)
    }
    Method (R002) { MBT0 = 1 MBT1 = 5 Return (MBT1) }
    Method (R003) { MBIG = Buffer () {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15} Return (MBIG) }
    Method (R004) { MX30 = 0xABC MX31 = 0x123 Return (MX30 + (MX31 << 16)) }
    Method (R005) { WO1 = 5 Return (WO1) }
    Method (R006) { WZ1 = 5 Return (WZ1) }
    Method (R007) { PD1 = 0xAB Return (PD1) }
    Method (R008) { QW1 = 0x1122334455667788 Return (QW1) }
    Method (R009) { AB1 = 0xBEEF Return (AB1) }
    Method (R010) { LKF = 7 Return (LKF) }
    Method (R011) { Return (EDG0) }
    Method (R012) { Return (OVR) }
    Method (R013) { DADR = 0xFED400A0 Return (DF0) }
    Method (R014) { IOW0 = 0x1234 IOB2 = 0x56 Return (IOW0) }
    Method (R015) { Return (IOD8) }
    Method (R016) { IOBT = 1 Return (IOBT) }
    Method (R017) { \PCI0.LPC0.PMB2 = 0x1234 Return (\PCI0.LPC0.PMBA) }
    Method (R018) { Return (\PCI0.BR1.DEVX.VID | (\PCI0.BR1.DEVX.DID << 16)) }
    Method (R019) { Return (\PCI1.DEVY.YV0) }
    Method (R020) { \EC0.BCAP = 0x1234 Return (\EC0.BCAP + (\EC0.TMP0 << 16)) }
    Method (R021) { Return (CENT | (HIC << 8)) }
    Method (R022) { IF10 = 0x5A IF11 = 0xBEEF Return (IF11 | (IF10 << 16)) }
    Method (R023) { BF1 = 0x11 BF2 = 0x22 Return (BF1 | (BF2 << 8)) }
    Method (R024) { Return (OEMF) }
    Method (R025) { Return (DSIG) }
    Method (R026) { Load (SSDR, DDBH) Return (\SSXV) }
    Method (R027) { Return (\PCI0.LPC0.REGD | (\EC0.ECOK << 4)) }
    Method (R028) { WZW = 0xF Return (WZW) }
    Method (R029) { IOQ8 = 0x1122334455667788 Return (IOQ8) }
    Method (R030) { MD04 = "AB" Return (MD04) }
    Method (R031) { MBIG = 0x55 Return (MBIG) }
    Method (R032) { Return (SSG0) }
}
