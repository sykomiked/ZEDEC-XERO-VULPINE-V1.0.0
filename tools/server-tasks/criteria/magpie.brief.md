# magpie — the complete brief

| | |
|---|---|
| matrix layer | 119 |
| icon group | L1 |
| wave spec | `wave10/119-magpie.md` |

## Purpose

the last copies, to paste again, pin or clear. Kept in memory only. zxvd has no clipboard method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| dBase | `-` | records | records | primary | planned: needs Lingua front-end |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |
| ALGOL 58 | `-` | formula | formulas | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (dBase): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "keep this copy" = not concealed (password field or marked private) AND within size AND app not excluded, in eve + kron witnessed
- history ring (64 slots, head - tail >= capacity drops the oldest unpinned) in kron
- bytes held exact, at most 64 x 49152, in trebago
- Never log or store clipboard content in tests
- inputs are lengths and flags only

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id magpie
matrix 119
name Magpie
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

