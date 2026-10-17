# Criteria — distaff

| | |
|---|---|
| app | `distaff` |
| matrix layer | 389 |
| icon group | L3 |
| wave spec | `wave32/389-distaff.md` |

## Purpose

Publishes and links your own Web4 site of pages, files and services with signed links: pages in the Hoard (`files.read`, `files.write`), published by CID (`aipi.put`, `web4.cid`, `spaces.set_cid`, `pspace.create`), envelopes (`web4.open`, `web4.envelope`, `web4.verify`), keys in escrow (`recovery.status`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `net`
- `peers`
- `ledger.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | hosting credits |
| FORTRAN | trebago | link ranking |
| Ada | eve | bandwidth limits |
| APL | kron | link matrix |
| MUMPS | myra | site tree |
| Visual Basic | dotnet | view and model |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id distaff
matrix 389        # the graph's slot IS the matrix layer
name Distaff
needs fs.read
needs fs.write
needs net
needs peers
needs ledger.read
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

