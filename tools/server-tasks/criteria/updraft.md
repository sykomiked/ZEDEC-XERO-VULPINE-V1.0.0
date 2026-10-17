# Criteria — updraft

| | |
|---|---|
| app | `updraft` |
| matrix layer | 214 |
| icon group | L2 |
| wave spec | `wave17/214-updraft.md` |

## Purpose

runs a radio and mesh signal station: pairs peers (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), relays messages (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`) and watches link quality (`net.peers`).

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | message log |
| FORTRAN | trebago | signal propagation |
| Ada | eve | transmit power limits |
| APL | kron | link matrix |
| MUMPS | myra | relay tree |
| Gleam | - | messaging |
| LabVIEW | - | signals |
| TRAC | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id updraft
matrix 214        # the graph's slot IS the matrix layer
name Updraft
needs net
needs peers
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

