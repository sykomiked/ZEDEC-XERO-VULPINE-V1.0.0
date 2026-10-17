# Criteria — linkman

| | |
|---|---|
| app | `linkman` |
| matrix layer | 200 |
| icon group | L2 |
| wave spec | `wave16/200-linkman.md` |

## Purpose

maps the mesh: every peer, route and name (`net.peers`, `peer.status`, `devmesh.devices`, `sys.net`, `rpc.discover`) as a live constellation you can inspect.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | peer register |
| FORTRAN | trebago | latency model |
| Ada | eve | trust limits |
| APL | kron | adjacency matrix |
| MUMPS | myra | topology tree |
| Gleam | - | messaging |
| Prolog | - | search |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id linkman
matrix 200        # the graph's slot IS the matrix layer
name Linkman
needs peers
needs net
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

