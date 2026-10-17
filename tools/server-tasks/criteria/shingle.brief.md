# shingle — the complete brief

| | |
|---|---|
| matrix layer | 213 |
| icon group | L2 |
| wave spec | `wave17/213-shingle.md` |

## Purpose

registers every program in its slot: checks the signed manifest's slot, layer, capabilities and budget at admission, and publishes and fetches blocks by content id (`aipi.put`, `aipi.get`, `web4.cid`), with admissions on the journal (`journal.query`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `net`
- `journal`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | registry ledger | primary | runs |
| FORTRAN | `trebago` | numeric | version ordering | primary | runs |
| Ada | `eve` | limits | refuses a manifest whose layer does not match its capabilities | primary | runs |
| APL | `kron` | arrays | slot table of 407 | primary | runs |
| MUMPS | `myra` | records | manifest records | primary | runs |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (ML): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "admit" (slot in its layer, the same rule as `zxapp_slot_layer`: L1 1-144, L2 145-271, L3 272-407
- every need known to `zxapp_cap_known`
- budget declared) in Ada as Eve + APL as KRON witnessed, refusing a manifest whose layer does not match
- version ordering (major.minor.patch) in FORTRAN as Trebago + APL as KRON witnessed
- slot table of 407 in APL as KRON
- registry ledger (admission number, slot, never reused) in COBOL as Sutra
- manifest records in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id shingle
matrix 213
name Shingle
needs fs.read
needs fs.write
needs net
needs journal
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

