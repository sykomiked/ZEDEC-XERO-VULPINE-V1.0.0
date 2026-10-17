# Criteria — overture

| | |
|---|---|
| app | `overture` |
| matrix layer | 252 |
| icon group | L2 |
| wave spec | `wave20/252-overture.md` |

## Purpose

Fronts first start (kernel/src/seats, kernel/src/ident, kernel/src/terms (the agreement), kernel/src/license, kernel/src/i18n, kernel/src/timesvc for zones). Seats and passkeys (`seats.add`, `seats.list`, `seats.set_lock`, `seats.auto`, `seats.set_auto`, `passkeys.register_begin`, `passkeys.register_finish`), language and simple mode (`xlate.languages`, `ui.get`, `ui.set`), network (`net.command`), moving in from an old machine (`devmesh.join`, `devmesh.confirm_sas`, `files.cross`, `spaces.list`), the clock (`sys.info`). Recording a licence acceptance, setting the time zone and setting the keyboard layout have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `peers`
- `net`
- `backup`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | setup log |
| FORTRAN | trebago | time zone and clock maths |
| Ada | eve | licence must be accepted per user |
| APL | kron | language and layout tables |
| MUMPS | myra | account records |
| Gleam | - | messaging |
| Raku | - | parsing |
| Kotlin | kotlin | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id overture
matrix 252        # the graph's slot IS the matrix layer
name Overture
needs sys.read
needs fs.read
needs fs.write
needs peers
needs net
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

