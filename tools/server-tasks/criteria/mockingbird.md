# Criteria — mockingbird

| | |
|---|---|
| app | `mockingbird` |
| matrix layer | 128 |
| icon group | L1 |
| wave spec | `wave3/128-mockingbird.md` |

## Purpose

emulate a ZXV device (the hwemu model) to try drivers and apps; machine facts from `sys.info`. zxvd has no emulator method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| APL | kron | arrays |
| MUMPS | myra | records |
| Simula 67 | - | simulation |
| B | - | low-level system |
| Racket | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id mockingbird
matrix 128        # the graph's slot IS the matrix layer
name Mockingbird
needs sys.read
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

