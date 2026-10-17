# Criteria — muster

| | |
|---|---|
| app | `muster` |
| matrix layer | 229 |
| icon group | L2 |
| wave spec | `wave19/229-muster.md` |

## Purpose

Fronts the driver layer (kernel/src/hwdrv: hwdrv_probe.c, hwdrv_core.c, usb.h, usbmsc.c, usbkbd.c; kernel/src/pci; kernel/src/acpi; kernel/src/hwemu for the compatibility model). Shows the machine (`sys.info`, `sys.disk`, `sys.net`, `power.status`) and checks for driver updates (`update.check`). A device inventory, USB authorisation and a compatibility-list report have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | device admission log |
| FORTRAN | trebago | bus timing and power figures |
| Ada | eve | USB authorisation rules |
| APL | kron | PCI/USB inventory grid |
| MUMPS | myra | device tree |
| BCPL | - | low-level system |
| PHP | php | reports |
| IPL | - | lists |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id muster
matrix 229        # the graph's slot IS the matrix layer
name Muster
needs sys.read
needs net
needs notify
provides <entry> num|truth <arg>[,<arg>...]
logic <lang> <entry> <<
<source>
.
check <entry> <arg>=<int>[,<int>...] = <answer>[,<answer>...]
```

Rules the loader enforces (a violation refuses the manifest):

- Every `provides` entry must have at least one `logic` line.
- Every `logic` entry must have a `check` line with exact answers.
- The checks' inputs are integers; answers are integers, `n/d`,
  `true`, `false`, `neither`, or the word `both` (two languages
  disagreeing).
- A truth entry's checks use the four-valued answers, never 0/1.
- Inline sources end with a line containing only `.`.

