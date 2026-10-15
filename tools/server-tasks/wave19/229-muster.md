# Wave 19 task: Muster (slot 229), Devices and drivers
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (015 tidewatch, 017 almanac), wave 1 (006 tideturn, 008 gatehouse), wave 3 (128 mockingbird).
Manifest: `slot 229`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/229-muster.svg`, `budget 233`, model `apps/muster_model.{h,c}`, blocks under kernel/src/apps/blocks/muster/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts the driver layer (kernel/src/hwdrv: hwdrv_probe.c, hwdrv_core.c, usb.h, usbmsc.c, usbkbd.c; kernel/src/pci; kernel/src/acpi; kernel/src/hwemu for the compatibility model). Shows the machine (`sys.info`, `sys.disk`, `sys.net`, `power.status`) and checks for driver updates (`update.check`). A device inventory, USB authorisation and a compatibility-list report have no method: use only existing methods; list what's missing in your report.
Uses: `use almanac 1` (uptime `days`, `hours`, `used_share`); `use gatehouse 1` ("is this use allowed"); `use tidewatch 1` (`cpu_x100` and `peak`); `use mockingbird 1` (memory map regions do not overlap, boot self-check); `use tideturn 1` (version ordering and "both signatures valid"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "trust this USB device" = the owner said yes AND its class is allowed AND it is not on the refused list, in Ada as Eve + APL as KRON witnessed
- (critical) power per port (sum of devices' mA within the port's limit) in FORTRAN as Trebago + APL as KRON witnessed
- bus timing and power figures exact in FORTRAN as Trebago
- PCI/USB inventory grid (bus x device x function, class, driver) in APL as KRON, with BAR overlap from Mockingbird's block
- device admission log as fixed records (device, decision, time), counts refusing on overflow, in COBOL as Sutra
- device tree (bus, device, driver, state) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a device's firmware descriptor reports class 0x08 (storage) while the ACPI tables describe another device at that address, giving BOTH; the test asserts state 3 and that the device runs in reduced mode (read-only, no DMA), neither trusted nor dropped. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: No device is trusted without the owner; a compatibility report is sent only on the owner's approval and carries no serial numbers. Tests use the hwemu model only.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, net, notify.
Planned (no runtime yet): BCPL, PHP, IPL.
