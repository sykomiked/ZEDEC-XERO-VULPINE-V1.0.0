# Criteria — ferry

| | |
|---|---|
| app | `ferry` |
| matrix layer | 12 |
| icon group | L1 |
| wave spec | `wave4/012-ferry.md` |

## Purpose

move files, spaces, keys (owner approves), settings (`files.cross`, `spaces.*`, Keepsake).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `peers`
- `backup`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| transfer plan totals (bytes, files) exact in trebago | trebago |
| progress x100 from the used_share block (`use used_share 1`) | — |
| "fits on the target" truth in sutra + kron witnessed | sutra, kron |
| per-space share in kron | kron |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| APL | kron | arrays |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id ferry
matrix 12        # the graph's slot IS the matrix layer
name Ferry
needs fs.read
needs fs.write
needs peers
needs backup
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

