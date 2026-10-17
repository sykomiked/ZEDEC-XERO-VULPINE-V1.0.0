# Criteria — kindred

| | |
|---|---|
| app | `kindred` |
| matrix layer | 44 |
| icon group | L1 |
| wave spec | `wave6/044-kindred.md` |

## Purpose

contacts as records kept in the Hoard (`files.read`, `files.write`), a contact's agent card checked with `web4.card_verify`, their node from `net.peers`, chat identity from `chat.me`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `peers`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| contact index and name-prefix search in myra | myra |
| field limits in eve | eve |
| likely duplicates (same card id or same name codes) in kron | kron |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| SNOBOL | - | text |
| Kotlin | kotlin | domain objects |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id kindred
matrix 44        # the graph's slot IS the matrix layer
name Kindred
needs fs.read
needs fs.write
needs peers
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

