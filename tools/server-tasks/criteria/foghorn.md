# Criteria — foghorn

| | |
|---|---|
| app | `foghorn` |
| matrix layer | 400 |
| icon group | L3 |
| wave spec | `wave33/400-foghorn.md` |

## Purpose

Runs navigation aids and harbour signals: hazards broadcast once a person approves (`social.post`, `chat.post`, `pspace.post`, `harmonic.encode`, `ehop.bands`), beacon health (`log.tail`, `journal.query`), places (`place.list`, `place.add`). Beacon and light control has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `journal`
- `location`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | light dues |
| FORTRAN | trebago | visibility range model |
| Ada | eve | signal timing limits |
| APL | kron | beacon grid |
| MUMPS | myra | harbour tree |
| JOVIAL | - | machine control |
| Go | go | messaging |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id foghorn
matrix 400        # the graph's slot IS the matrix layer
name Foghorn
needs peers
needs net
needs journal
needs location
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

