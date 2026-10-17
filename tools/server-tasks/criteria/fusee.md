# Criteria — fusee

| | |
|---|---|
| app | `fusee` |
| matrix layer | 247 |
| icon group | L2 |
| wave spec | `wave20/247-fusee.md` |

## Purpose

Fronts the boot chain: the UEFI stubs (kernel/arch/x86_64/efi_stub.c, kernel/arch/arm64/efi_stub.c, kernel/arch/efi_models.h), measured boot (kernel/src/bootattest), boot features (kernel/src/bootfeat), A/B slots (kernel/src/loader/abupdate.c), update checks (kernel/src/update, zx_upcheck.c) and hardware keys (kernel/src/hwkey). Signed update check (`update.check`), the journal's chain (`journal.verify`, `journal.query`), switches (`features.get`), the machine (`sys.info`). Secure Boot keys, boot entries, A/B slot selection, firmware capsules, microcode and reading the measured-boot log have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `journal`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | boot-entry and slot ledger |
| FORTRAN | trebago | firmware version arithmetic |
| Ada | eve | refuses unsigned stubs and rollback below the floor |
| APL | kron | measurement table |
| MUMPS | myra | firmware inventory tree |
| Nim | - | low-level system |
| Racket | - | parsing |
| Forth | forth | small interpreters |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fusee
matrix 247        # the graph's slot IS the matrix layer
name Fusee
needs sys.read
needs journal
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

