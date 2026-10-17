# Criteria — hydrophone

| | |
|---|---|
| app | `hydrophone` |
| matrix layer | 379 |
| icon group | L3 |
| wave spec | `wave31/379-hydrophone.md` |

## Purpose

Tracks marine life and ship noise with underwater listening stations, sharing sightings and protecting migration routes: stations (`net.peers`, `devmesh.devices`), messages relayed (`ehop.seal`, `ehop.open`, `harmonic.encode`), places (`place.list`), records (`files.read`, `files.write`, `search.query`). Hydrophone audio has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `location`
- `fs.read`
- `fs.write`
- `search`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | shipping levy |
| FORTRAN | trebago | acoustic signal processing |
| Ada | eve | zone speed limits |
| APL | kron | spectrogram arrays |
| MUMPS | myra | species tree |
| IPL | - | lists |
| Prolog | - | search |
| Racket | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hydrophone
matrix 379        # the graph's slot IS the matrix layer
name Hydrophone
needs peers
needs net
needs location
needs fs.read
needs fs.write
needs search
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

