# Criteria — portolan

| | |
|---|---|
| app | `portolan` |
| matrix layer | 344 |
| icon group | L3 |
| wave spec | `wave29/344-portolan.md` |

## Purpose

Surveys and publishes maps of land, buildings and routes, merging field notes, drone scans and old charts: places (`place.here`, `place.list`, `place.add`), notes and scans in the Hoard (`files.read`, `files.write`, `search.query`), the published map by CID (`aipi.put`, `web4.cid`, `spaces.set_cid`). Drone flight logs and chart image import have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `search`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | parcel registry fees |
| FORTRAN | trebago | projection maths |
| Ada | eve | no-fly zone limits |
| APL | kron | elevation grids |
| MUMPS | myra | place hierarchy |
| Logo | logo | geometry |
| MATLAB | - | matrices and images |
| Ladder logic | - | machine control |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id portolan
matrix 344        # the graph's slot IS the matrix layer
name Portolan
needs location
needs fs.read
needs fs.write
needs search
needs net
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

