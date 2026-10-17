# Criteria — annulus

| | |
|---|---|
| app | `annulus` |
| matrix layer | 285 |
| icon group | L3 |
| wave spec | `wave24/285-annulus.md` |

## Purpose

manages medicines for a household or small pharmacy: prescriptions, doses, refills and interaction warnings in the Hoard (`files.read`, `files.write`, `search.query`), with notices (`notes.list`). zxvd has no method for a reminder notice post (as Sedge reports): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `notify`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | dispensing records |
| FORTRAN | trebago | dose kinetics |
| Ada | eve | maximum dose limits |
| APL | kron | interaction grid |
| MUMPS | myra | patient medication tree |
| Erlang | - | messaging |
| Groovy | - | scripting |
| Raku | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id annulus
matrix 285        # the graph's slot IS the matrix layer
name Annulus
needs fs.read
needs fs.write
needs search
needs notify
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

