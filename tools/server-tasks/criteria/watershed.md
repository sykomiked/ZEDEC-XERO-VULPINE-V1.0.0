# Criteria — watershed

| | |
|---|---|
| app | `watershed` |
| matrix layer | 221 |
| icon group | L2 |
| wave spec | `wave18/221-watershed.md` |

## Purpose

Runs cooperative projects: the pooled fund and member shares in the books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`), escrow for disputed amounts (`ledger.hold`, `ledger.holds`, `ledger.release`), member votes (`spaces.propose`, `spaces.vote`, `spaces.get`), posts (`social.post`, `pspace.post`), market sales (`net.offers`, `net.command` sell, `mesh.books`), shared tasks in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | member shares |
| FORTRAN | trebago | grant model |
| Ada | eve | spending caps |
| APL | kron | contribution grid |
| MUMPS | myra | member tree |
| Gleam | - | messaging |
| LISP | lisp | lists |
| Prolog | - | rules |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id watershed
matrix 221        # the graph's slot IS the matrix layer
name Watershed
needs ledger.read
needs ledger.post
needs peers
needs net
needs fs.read
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

