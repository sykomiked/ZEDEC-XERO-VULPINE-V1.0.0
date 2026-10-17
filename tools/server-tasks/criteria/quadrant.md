# Criteria — quadrant

| | |
|---|---|
| app | `quadrant` |
| matrix layer | 394 |
| icon group | L3 |
| wave spec | `wave33/394-quadrant.md` |

## Purpose

Links remote places by satellite: book passes, relay messages and earth images, share ground-station time. Bands and frames (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`, `freight.pack`, `freight.unpack`), peers (`net.peers`, `peer.status`), images in the Hoard (`files.read`, `files.write`). Satellite pass data and radio hardware have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `fs.read`
- `fs.write`
- `location`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | pass billing |
| FORTRAN | trebago | orbit prediction |
| Ada | eve | transmit power limits |
| APL | kron | imagery arrays |
| MUMPS | myra | station tree |
| Elixir | - | messaging |
| AWK | - | text |
| MATLAB | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id quadrant
matrix 394        # the graph's slot IS the matrix layer
name Quadrant
needs net
needs peers
needs fs.read
needs fs.write
needs location
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

