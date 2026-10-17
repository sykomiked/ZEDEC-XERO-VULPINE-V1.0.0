# Criteria — consort

| | |
|---|---|
| app | `consort` |
| matrix layer | 392 |
| icon group | L3 |
| wave spec | `wave33/392-consort.md` |

## Purpose

Finds and connects people with shared skills into project teams, mentoring pairs and trusted introductions: the mesh map (`net.peers`, `peer.status`), contacts (`files.read`, `files.write`), introductions as signed envelopes held for consent (`web4.post`, `web4.consent`, `web4.verify`), the circle (`social.groups`, `social.post`).

## Capabilities (needs — the exact registry names)

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
| COBOL | sutra | contribution ledger |
| FORTRAN | trebago | match scoring |
| Ada | eve | contact limits |
| APL | kron | skill matrix |
| MUMPS | myra | network tree |
| Go | go | messaging |
| Prolog | - | search |
| Oberon | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id consort
matrix 392        # the graph's slot IS the matrix layer
name Consort
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

