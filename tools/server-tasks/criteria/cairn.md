# Criteria — cairn

| | |
|---|---|
| app | `cairn` |
| matrix layer | 271 |
| icon group | L2 |
| wave spec | `wave22/271-cairn.md` |

## Purpose

Runs community governance: proposals and votes (`spaces.propose`, `spaces.vote`, `spaces.get`, `spaces.set_members`), the circle (`social.post`, `social.feed`), signed decisions checked (`web4.verify`), the trail (`journal.query`), rulings saved (`files.write`).

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `journal`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | member register |
| FORTRAN | trebago | vote tallies |
| Ada | eve | quorum rules |
| APL | kron | ballot matrix |
| MUMPS | myra | council tree |
| Prolog | - | rules |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cairn
matrix 271        # the graph's slot IS the matrix layer
name Cairn
needs peers
needs net
needs journal
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

