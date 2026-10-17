# muster — the complete brief

| | |
|---|---|
| matrix layer | 229 |
| icon group | L2 |
| wave spec | `wave19/229-muster.md` |

## Purpose

Fronts the driver layer (kernel/src/hwdrv: hwdrv_probe.c, hwdrv_core.c, usb.h, usbmsc.c, usbkbd.c; kernel/src/pci; kernel/src/acpi; kernel/src/hwemu for the compatibility model). Shows the machine (`sys.info`, `sys.disk`, `sys.net`, `power.status`) and checks for driver updates (`update.check`). A device inventory, USB authorisation and a compatibility-list report have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | device admission log | primary | runs |
| FORTRAN | `trebago` | numeric | bus timing and power figures | primary | runs |
| Ada | `eve` | limits | USB authorisation rules | primary | runs |
| APL | `kron` | arrays | PCI/USB inventory grid | primary | runs |
| MUMPS | `myra` | records | device tree | primary | runs |
| BCPL | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| PHP | `php` | report | reports | primary | planned: SDK host only, no in-app runtime yet |
| IPL | `-` | lists | lists | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (BCPL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **php** (PHP): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "trust this USB device" = the owner said yes AND its class is allowed AND it is not on the refused list, in Ada as Eve + APL as KRON witnessed
- - (critical) power per port (sum of devices' mA within the port's limit) in FORTRAN as Trebago + APL as KRON witnessed
- - bus timing and power figures exact in FORTRAN as Trebago
- - PCI/USB inventory grid (bus x device x function, class, driver) in APL as KRON, with BAR overlap from Mockingbird's block
- - device admission log as fixed records (device, decision, time), counts refusing on overflow, in COBOL as Sutra
- - device tree (bus, device, driver, state) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a device's firmware descriptor reports class 0x08 (storage) while the ACPI tables describe another device at that address, giving BOTH
- the test asserts state 3 and that the device runs in reduced mode (read-only, no DMA), neither trusted nor dropped. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id muster
matrix 229
name Muster
needs sys.read
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

