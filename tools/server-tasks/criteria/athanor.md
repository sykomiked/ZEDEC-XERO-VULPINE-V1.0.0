# Criteria — athanor

| | |
|---|---|
| app | `athanor` |
| matrix layer | 351 |
| icon group | L3 |
| wave spec | `wave29/351-athanor.md` |

## Purpose

Plans and runs chemistry or brewing processes: recipes, batch records and hazard checks in the Hoard (`files.read`, `files.write`, `search.query`), heat and power readings (`sys.info`, `power.status`), alarms (`log.tail`, `journal.query`). Process sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `sys.read`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | batch costing |
| FORTRAN | trebago | reaction kinetics |
| Ada | eve | pressure and heat limits |
| APL | kron | concentration arrays |
| MUMPS | myra | batch lineage tree |
| JOSS | - | formulas |
| JCL | - | jobs |
| Simula I | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id athanor
matrix 351        # the graph's slot IS the matrix layer
name Athanor
needs fs.read
needs fs.write
needs search
needs sys.read
needs journal
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

