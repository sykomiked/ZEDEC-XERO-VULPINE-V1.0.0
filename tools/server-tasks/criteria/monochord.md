# Criteria — monochord

| | |
|---|---|
| app | `monochord` |
| matrix layer | 343 |
| icon group | L3 |
| wave spec | `wave28/343-monochord.md` |

## Purpose

runs optics and spectroscopy experiments: captures spectra, fits lines, logs runs and shares reproducible results (`files.read`, `files.write`, `search.query`, `aipi.put`, `web4.cid`). zxvd has no method for spectrometer readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `camera`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | grant spend |
| FORTRAN | trebago | spectral fitting |
| Ada | eve | laser exposure limits |
| APL | kron | spectrum arrays |
| MUMPS | myra | sample hierarchy |
| LabVIEW | - | signals |
| Julia | - | matrices and images |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id monochord
matrix 343        # the graph's slot IS the matrix layer
name Monochord
needs fs.read
needs fs.write
needs search
needs camera
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

