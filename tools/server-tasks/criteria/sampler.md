# Criteria — sampler

| | |
|---|---|
| app | `sampler` |
| matrix layer | 347 |
| icon group | L3 |
| wave spec | `wave29/347-sampler.md` |

## Purpose

Catalogues a museum or private collection: objects, provenance, condition reports, loans and exhibition labels in the Hoard (`files.read`, `files.write`, `search.query`), versions (`backup.versions`), the record proven by the journal (`journal.query`, `journal.verify`), labels printed (`print.submit`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `backup`
- `journal`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | loan and insurance values |
| FORTRAN | trebago | decay rates |
| Ada | eve | humidity limits |
| APL | kron | catalogue arrays |
| MUMPS | myra | provenance tree |
| Objective-C | - | domain objects |
| SAS | - | reports |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sampler
matrix 347        # the graph's slot IS the matrix layer
name Sampler
needs fs.read
needs fs.write
needs search
needs backup
needs journal
needs print
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

