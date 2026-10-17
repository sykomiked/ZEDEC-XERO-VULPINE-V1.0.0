# Criteria — armature

| | |
|---|---|
| app | `armature` |
| matrix layer | 297 |
| icon group | L3 |
| wave spec | `wave25/297-armature.md` |

## Purpose

an invention lab notebook: experiments, prototypes, parts, results and dated proof of who invented what (`files.read`, `files.write`, `journal.query`, `web4.cid`, `aipi.put`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | prototype costs |
| FORTRAN | trebago | test calculations |
| Ada | eve | safety limits |
| APL | kron | result matrix |
| MUMPS | myra | design revision tree |
| SNOBOL | - | text |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id armature
matrix 297        # the graph's slot IS the matrix layer
name Armature
needs fs.read
needs fs.write
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

