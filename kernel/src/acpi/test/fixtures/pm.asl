/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pm.asl — the power-management objects a laptop DSDT carries: sleep
 * packages, _PTS/_WAK, batteries, lid, power button, thermal zones,
 * processors (Processor() with _PSS/_PCT/_PPC and ACPI0007 with _CPC),
 * backlight, GPE methods and an embedded controller with a query method.
 * test_aml_pm.c drives it through the typed helpers and acpi_pm. */
DefinitionBlock ("", "DSDT", 2, "ZXV", "PMTEST", 0x00000001)
{
    Name (_S0_, Package () { 0, 0, 0, 0 })
    Name (_S1_, Package () { 0x0401 })
    Name (_S3_, Package () { 5, 5, 0, 0 })
    Name (_S5_, Package () { 7, 6, 0, 0 })

    Name (PTSV, 0xFF)
    Name (WAKV, 0xFF)
    Name (SSTV, 0xFF)
    Name (SBIN, 0)
    Name (INIC, 0)
    Name (BADI, 0)
    Name (FUNI, 0)
    Name (L05C, 0)
    Name (E06C, 0)
    Name (LIDS, 1)
    Name (BRTL, 50)
    Name (Q11C, 0)
    Name (BREM, 3000)

    Method (_PTS, 1) { PTSV = Arg0 }
    Method (_WAK, 1) { WAKV = Arg0 Return (Package () { 0, 0 }) }
    Scope (\_SI) { Method (_SST, 1) { SSTV = Arg0 } }

    Scope (\_GPE) {
        Method (_L05) { L05C++ }
        Method (_E06) { E06C++ }
        Method (_L1F) { Notify (\_SB.PWRB, 0x80) }
    }

    Scope (\_PR) {
        Processor (CPU0, 0, 0x00000810, 6) {
            Name (_PCT, Package () {
                ResourceTemplate () { Register (FFixedHW, 0, 0, 0x0000000000000199, ,) },
                ResourceTemplate () { Register (FFixedHW, 0, 0, 0x0000000000000198, ,) }
            })
            Name (_PSS, Package () {
                Package () { 2400, 15000, 10, 10, 0x1800, 0x1800 },
                Package () { 1800, 10000, 10, 10, 0x1200, 0x1200 },
                Package () { 800, 4000, 10, 10, 0x0800, 0x0800 }
            })
            Method (_PPC) { Return (1) }
        }
        Processor (CPU1, 1, 0x00000810, 6) {
            Name (_PSS, Package () { Package () { 1000, 5000, 10, 10, 1, 1 } })
        }
    }

    Scope (\_SB) {
        Method (_INI) { SBIN++ }
        Device (CPU2) {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
            Name (_CPC, Package () {
                23, 3,
                ResourceTemplate () { Register (SystemMemory, 32, 0, 0xFED50000, 3) },
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
            })
        }
        Device (CPU3) {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
            Name (_CPC, Package () { 99, 3 })
        }
        Device (PCI0) {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_ADR, 0)
            Method (_INI) { INIC++ }
            Device (ABS0) {
                Name (_ADR, 0x00010000)
                Name (_STA, 0)
                Method (_INI) { BADI++ }
                Device (ABSC) { Name (_ADR, 0) Method (_INI) { BADI++ } }
            }
            Device (FUN0) {
                Name (_ADR, 0x00020000)
                Method (_STA) { Return (0x08) }
                Method (_INI) { BADI++ }
                Device (FUNC) { Name (_ADR, 0) Method (_INI) { FUNI++ } }
            }
            Device (ERR0) {
                Name (_ADR, 0x00030000)
                Method (_STA) { Return (1 / Zero) }
                Method (_INI) { BADI++ }
            }
            Device (GFX0) {
                Name (_ADR, 0x00020000)
                Device (LCD0) {
                    Name (_ADR, 0x0400)
                    Method (_BCL) { Return (Package () { 80, 40, 10, 20, 40, 60, 80, 100 }) }
                    Method (_BCM, 1) { BRTL = Arg0 }
                    Method (_BQC) { Return (BRTL) }
                }
            }
            Device (EC0) {
                Name (_HID, EisaId ("PNP0C09"))
                Name (_GPE, 0x17)
                Name (_CRS, ResourceTemplate () {
                    IO (Decode16, 0x62, 0x62, 0, 1)
                    IO (Decode16, 0x66, 0x66, 0, 1)
                })
                OperationRegion (ECOR, EmbeddedControl, 0, 0x100)
                Field (ECOR, ByteAcc, Lock, Preserve) { Offset (0x40), ECLD, 8 }
                Method (_Q11) { Q11C++ LIDS = ECLD Notify (\_SB.LID0, 0x80) }
            }
        }
        Device (BAT0) {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, 0)
            Method (_STA) { Return (0x1F) }
            Method (_BIX) {
                Return (Package () {
                    1, 0, 50000, 40000, 1, 11400, 4000, 2000, 123, 100000,
                    0xFFFFFFFF, 0xFFFFFFFF, 1000, 1000, 100, 100,
                    "MODEL", "1234", "LION", "ZXV", 0
                })
            }
            Method (_BST) { Return (Package () { 1, 1500, BREM, 11800 }) }
        }
        Device (BAT1) {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, 1)
            Method (_STA) { Return (0x1F) }
            Method (_BIF) {
                Return (Package () {
                    1, 5200, 5000, 1, 10800, 500, 200, 1, 1, "M2", "S2", "LION", "ZXV"
                })
            }
            Method (_BST) { Return (Package () { 2, 0xFFFFFFFF, 6000, 12000 }) }
        }
        Device (BAT2) {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, 2)
            Method (_STA) { Return (0x0F) }
        }
        Device (LID0) {
            Name (_HID, EisaId ("PNP0C0D"))
            Method (_LID) { Return (LIDS) }
        }
        Device (PWRB) {
            Name (_HID, EisaId ("PNP0C0C"))
        }
    }

    Scope (\_TZ) {
        ThermalZone (TZ00) {
            Method (_TMP) { Return (3132) }
            Method (_PSV) { Return (3532) }
            Method (_CRT) { Return (3732) }
            Method (_HOT) { Return (3632) }
        }
        ThermalZone (TZ01) {
            Method (_TMP) { Return (2632) }
        }
    }
}
