# Criteria — gait

| | |
|---|---|
| app | `gait` |
| matrix layer | 123 |
| icon group | L1 |
| wave spec | `wave10/123-gait.md` |

## Purpose

where time and energy go: consumers by energy (`power.top`, `power.status`), the swarm's cycles (`swarm.state`), machine facts (`sys.info`), events (`log.tail`). zxvd has no sampling-profiler method: use only existing methods; list what's missing in your report.

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
| Racket | - | parsing |
| ML | - | typed data |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id gait
matrix 123        # the graph's slot IS the matrix layer
name Gait
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

