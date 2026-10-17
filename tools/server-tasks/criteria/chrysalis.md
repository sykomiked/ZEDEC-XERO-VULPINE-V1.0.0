# Criteria — chrysalis

| | |
|---|---|
| app | `chrysalis` |
| matrix layer | 268 |
| icon group | L2 |
| wave spec | `wave22/268-chrysalis.md` |

## Purpose

Fronts guest machines (kernel/src/hwemu with hwe.c and hwa.c, kernel/src/virtio, kernel/src/legacy, kernel/src/capctl; no hypervisor exists yet). The host (`sys.info`, `sys.disk`), energy and budgets (`power.top`, `power.status`, `power.budget`), switches (`features.get`), guest disk images in the Hoard (`files.read`, `files.write`). Creating, starting and pausing a guest, and the driver machine, have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | guest ledger |
| FORTRAN | trebago | CPU and memory shares |
| Ada | eve | guest isolation limits |
| APL | kron | guest table |
| MUMPS | myra | guest disk images |
| Crystal | - | low-level system |
| WebAssembly | wasm | sandboxing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id chrysalis
matrix 268        # the graph's slot IS the matrix layer
name Chrysalis
needs sys.read
needs fs.read
needs fs.write
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

