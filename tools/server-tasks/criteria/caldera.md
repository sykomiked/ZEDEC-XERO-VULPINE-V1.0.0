# Criteria — caldera

| | |
|---|---|
| app | `caldera` |
| matrix layer | 259 |
| icon group | L2 |
| wave spec | `wave21/259-caldera.md` |

## Purpose

Watches natural hazards and pushes local alerts: events (`journal.query`, `log.tail`), the place (`place.here`), local alerts to the mesh only after a person confirms (`chat.post`, `pspace.post`), neighbours' nodes (`net.peers`, `devmesh.devices`). Hazard sensor feeds have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `journal`
- `location`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident records |
| FORTRAN | trebago | seismic model |
| Ada | eve | evacuation thresholds |
| APL | kron | hazard grid |
| MUMPS | myra | region tree |
| Elixir | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id caldera
matrix 259        # the graph's slot IS the matrix layer
name Caldera
needs journal
needs location
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

