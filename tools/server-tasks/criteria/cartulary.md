# Criteria — cartulary

| | |
|---|---|
| app | `cartulary` |
| matrix layer | 145 |
| icon group | L2 |
| wave spec | `wave12/145-cartulary.md` |

## Purpose

indexed, versioned records in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`), found with `search.query`, each version kept with `backup.versions` and `backup.list`, and proven years later against the journal (`journal.query`, `journal.verify`). zxvd has no method for a retention-hold flag in the daemon (kept as a Hoard file instead): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `backup`
- `journal`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | retention schedules |
| FORTRAN | trebago | storage growth model |
| Ada | eve | retention limits |
| APL | kron | index grid |
| MUMPS | myra | record hierarchy |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |
| Prolog | - | rules |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cartulary
matrix 145        # the graph's slot IS the matrix layer
name Cartulary
needs fs.read
needs fs.write
needs search
needs backup
needs journal
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

