# monochord — the complete brief

| | |
|---|---|
| matrix layer | 343 |
| icon group | L3 |
| wave spec | `wave28/343-monochord.md` |

## Purpose

runs optics and spectroscopy experiments: captures spectra, fits lines, logs runs and shares reproducible results (`files.read`, `files.write`, `search.query`, `aipi.put`, `web4.cid`). zxvd has no method for spectrometer readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `camera`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grant spend | primary | runs |
| FORTRAN | `trebago` | numeric | spectral fitting | primary | runs |
| Ada | `eve` | limits | laser exposure limits | primary | runs |
| APL | `kron` | arrays | spectrum arrays | primary | runs |
| MUMPS | `myra` | records | sample hierarchy | primary | runs |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (LabVIEW): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `laser_ok` (laser class and exposure time inside the bench's declared limits, shutter interlock closed) in Ada as Eve + APL as KRON witnessed
- `line_fit` (peak centre from the integer-weighted centroid of the counts) in FORTRAN as Trebago + APL as KRON witnessed
- `grant_spend` (in minor units) in COBOL as Sutra
- `spectra` arrays in APL as KRON
- `samples` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id monochord
matrix 343
name Monochord
needs fs.read
needs fs.write
needs search
needs camera
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

