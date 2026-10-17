# Criteria — syzygy

| | |
|---|---|
| app | `syzygy` |
| matrix layer | 303 |
| icon group | L3 |
| wave spec | `wave25/303-syzygy.md` |

## Purpose

two-party agreements witnessed in two languages: drafting, signing, obligations and disputes held fairly (`files.read`, `files.write`, `journal.query`, `web4.verify`, `pspace.post`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | contract payments |
| FORTRAN | trebago | penalty calculations |
| Ada | eve | obligation deadlines |
| APL | kron | clause matrix |
| MUMPS | myra | party hierarchy |
| Eiffel | - | contracts |
| Solidity | - | contracts |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id syzygy
matrix 303        # the graph's slot IS the matrix layer
name Syzygy
needs fs.read
needs fs.write
needs journal
needs peers
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

