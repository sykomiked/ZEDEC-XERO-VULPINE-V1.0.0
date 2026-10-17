# Criteria — plinth

| | |
|---|---|
| app | `plinth` |
| matrix layer | 254 |
| icon group | L2 |
| wave spec | `wave21/254-plinth.md` |

## Purpose

Fronts installation (install/install_zxv.py and install/transactional_disk.py, host only today; build_system/mk_golden_disc.sh; kernel/src/blockdev; kernel/src/zxvfs; kernel/src/loader/abupdate.c for the A/B slots; kernel/src/hwkey for sealing). Room and discs (`sys.disk`, `sys.info`), the backup check (`backup.verify`), the signed image check (`update.check`). Listing block devices, partitioning, formatting and sealing a key to the machine have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `backup`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | install transaction log |
| FORTRAN | trebago | partition sizing |
| Ada | eve | refuses to touch a disc that is not blank or not confirmed |
| APL | kron | block maps |
| MUMPS | myra | partition table records |
| Zig | - | low-level system |
| Python | python | AI |
| Simula I | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id plinth
matrix 254        # the graph's slot IS the matrix layer
name Plinth
needs sys.read
needs fs.read
needs fs.write
needs backup
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

