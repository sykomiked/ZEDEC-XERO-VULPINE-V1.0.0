# Criteria — atelier

| | |
|---|---|
| app | `atelier` |
| matrix layer | 393 |
| icon group | L3 |
| wave spec | `wave33/393-atelier.md` |

## Purpose

Make your own app with no code: compose existing apps and blocks (Loom), register them in their slot (Shingle), start from a template (Freshet), check every language they use (Dragoman), lay out the screens (Trestle), test in the Cage and publish to your space or the market by content id (`aipi.put`, `aipi.publish`, `web4.cid`, `spaces.set_cid`, `net.offers`), with test runs (`play.run`) and files (`files.read`, `files.write`). Cage runs and manifest checks have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `net`
- `peers`
- `swarm`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | publication ledger |
| FORTRAN | trebago | budget totals |
| Ada | eve | contract and capability checks |
| APL | kron | block grid |
| MUMPS | myra | app tree |
| OCaml | - | parsing |
| TeX | - | typesetting |
| WebAssembly | wasm | sandboxing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id atelier
matrix 393        # the graph's slot IS the matrix layer
name Atelier
needs fs.read
needs fs.write
needs net
needs peers
needs swarm
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

