# Criteria — albedo

| | |
|---|---|
| app | `albedo` |
| matrix layer | 276 |
| icon group | L3 |
| wave spec | `wave23/276-albedo.md` |

## Purpose

checks news claims against sources (`search.query`, `files.read`, `web4.feed`, `web4.verify`), showing which are confirmed, refuted or still disputed between credible witnesses.

## Capabilities (needs — the exact registry names)

- `search`
- `fs.read`
- `net`
- `journal`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | source subscriptions |
| FORTRAN | trebago | credibility scores |
| Ada | eve | publish limits |
| APL | kron | claim matrix |
| MUMPS | myra | citation tree |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |
| Prolog | - | rules |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id albedo
matrix 276        # the graph's slot IS the matrix layer
name Albedo
needs search
needs fs.read
needs net
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

