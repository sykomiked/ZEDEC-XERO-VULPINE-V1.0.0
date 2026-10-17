# Criteria — warding

| | |
|---|---|
| app | `warding` |
| matrix layer | 173 |
| icon group | L2 |
| wave spec | `wave14/173-warding.md` |

## Purpose

issues and revokes keys for doors, devices and accounts from one place (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `seats.list`). zxvd has no method for door lock control (as Homestead reports): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | key register |
| FORTRAN | trebago | entropy checks |
| Ada | eve | expiry limits |
| APL | kron | access grid |
| MUMPS | myra | holder tree |
| Nim | - | low-level system |
| Kotlin | kotlin | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id warding
matrix 173        # the graph's slot IS the matrix layer
name Warding
needs sys.read
needs peers
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

