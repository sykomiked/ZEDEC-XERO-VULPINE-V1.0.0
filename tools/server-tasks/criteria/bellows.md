# Criteria — bellows

| | |
|---|---|
| app | `bellows` |
| matrix layer | 59 |
| icon group | L1 |
| wave spec | `wave7/059-bellows.md` |

## Purpose

requests through the companion (`swarm.ask`, `model.info`) under the owner's budgets (`power.budget`, `power.status`), results held until accepted (M3), then saved (`files.write`). Image and voice generation have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `swarm`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| FORTRAN | trebago | exact numerics |
| Python | python | AI |
| MATLAB | - | matrices and images |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id bellows
matrix 59        # the graph's slot IS the matrix layer
name Bellows
needs ai.ask
needs swarm
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

