# Criteria — graft

| | |
|---|---|
| app | `graft` |
| matrix layer | 124 |
| icon group | L1 |
| wave spec | `wave10/124-graft.md` |

## Purpose

commits, history and merge of folders in the Hoard; file versions from `backup.versions` and `backup.list`, bring one back with `backup.restore` (never over what is there); work through `files.*`. zxvd has no commit, branch or diff method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `backup`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| APL | kron | arrays |
| MUMPS | myra | records |
| Elm | - | typed data |
| OCaml | - | parsing |
| Bourne shell | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id graft
matrix 124        # the graph's slot IS the matrix layer
name Graft
needs fs.read
needs fs.write
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

