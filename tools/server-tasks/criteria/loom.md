# Criteria — loom

| | |
|---|---|
| app | `loom` |
| matrix layer | 265 |
| icon group | L2 |
| wave spec | `wave22/265-loom.md` |

## Purpose

Fronts app composition (kernel/src/apps/zxapp.c: manifests, `use`, `provides`, at most 8 `use` lines (ZXAPP_USES); kernel/src/modbind; kernel/src/immigration; docs/CAGE.md). Boards saved in the Hoard (`files.read`, `files.write`), test runs on scratch state (`play.run`), a composed app stored and published by CID (`aipi.put`, `aipi.publish`, `web4.cid`), its functions tried in the sandbox (`aipi.execute`). A Cage run and a manifest-check method have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `swarm`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | composition log |
| FORTRAN | trebago | budget totals per wire |
| Ada | eve | contract checks on each wire |
| APL | kron | board grid |
| MUMPS | myra | block tree |
| Bourne shell | - | jobs |
| Racket | - | parsing |
| B | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id loom
matrix 265        # the graph's slot IS the matrix layer
name Loom
needs fs.read
needs fs.write
needs swarm
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

