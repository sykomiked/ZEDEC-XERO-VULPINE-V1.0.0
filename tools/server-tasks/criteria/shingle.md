# Criteria — shingle

| | |
|---|---|
| app | `shingle` |
| matrix layer | 213 |
| icon group | L2 |
| wave spec | `wave17/213-shingle.md` |

## Purpose

registers every program in its slot: checks the signed manifest's slot, layer, capabilities and budget at admission, and publishes and fetches blocks by content id (`aipi.put`, `aipi.get`, `web4.cid`), with admissions on the journal (`journal.query`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `net`
- `journal`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | registry ledger |
| FORTRAN | trebago | version ordering |
| Ada | eve | refuses a manifest whose layer does not match its capabilities |
| APL | kron | slot table of 407 |
| MUMPS | myra | manifest records |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id shingle
matrix 213        # the graph's slot IS the matrix layer
name Shingle
needs fs.read
needs fs.write
needs net
needs journal
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

