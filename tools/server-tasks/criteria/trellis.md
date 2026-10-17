# Criteria — trellis

| | |
|---|---|
| app | `trellis` |
| matrix layer | 29 |
| icon group | L1 |
| wave spec | `wave1/029-trellis.md` |

## Purpose

pair (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), trust and revoke (`devmesh.devices`, `devmesh.status`, `devmesh.revoke`), phones (`phone.pair`, `phone.list`, `phone.revoke`), peers (`net.peers`, `net.command` off/lan/online).

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
| Ada | eve | limits |
| MUMPS | myra | records |
| Go | go | messaging |
| Elm | - | view and model |
| Elixir | - | fault handling |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id trellis
matrix 29        # the graph's slot IS the matrix layer
name Trellis
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

