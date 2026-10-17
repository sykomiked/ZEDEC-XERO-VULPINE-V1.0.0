# Criteria — hamlet

| | |
|---|---|
| app | `hamlet` |
| matrix layer | 96 |
| icon group | L1 |
| wave spec | `wave4/096-hamlet.md` |

## Purpose

the circle's posts, replies and reactions (`social.feed`, `social.post`, `social.react`, `social.groups`, `social.join`, `social.leave`); gifts to a member through `web4.post` with amount_minor, held until `web4.consent`.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| FORTRAN | trebago | exact numerics |
| COBOL | sutra | money and ledgers |
| C++ | cpp | money and ledgers |
| Erlang | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hamlet
matrix 96        # the graph's slot IS the matrix layer
name Hamlet
needs peers
needs net
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

