# tarn — the complete brief

| | |
|---|---|
| matrix layer | 224 |
| icon group | L2 |
| wave spec | `wave18/224-tarn.md` |

## Purpose

Guides breathing and focus sessions and logs calm and sleep over time (advice only): sessions and logs in the Hoard (`files.read`, `files.write`), quiet windows and notices (`notes.list`, `screen.status`), the clock from `sys.info`. Readings from a heart-rate or sleep sensor have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `screen.time`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | session log | primary | runs |
| FORTRAN | `trebago` | numeric | breath rhythm model | primary | runs |
| Ada | `eve` | limits | session limits | primary | runs |
| APL | `kron` | arrays | mood grid | primary | runs |
| MUMPS | `myra` | records | wellbeing record tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - (critical) breath pacing (inhale, hold, exhale ms per cycle
- cycles per session) exact in FORTRAN as Trebago + APL as KRON witnessed
- - (critical) session limits (1..60 minutes, a hold at most 7000 ms, no hold for a person who marked it unsafe) in Ada as Eve + APL as KRON witnessed
- - mood grid (days x calm, sleep minutes, self-report) in APL as KRON
- - session log as fixed records (sessions, minutes, refusing on overflow) in COBOL as Sutra
- - wellbeing record tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a self-report of stress 2/10 (calm) and a heart rate of 118 bpm outside Wellspring's range give BOTH
- the test asserts state 3 and that both are recorded and shown, with neither "all is well" nor an alarm. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id tarn
matrix 224
name Tarn
needs fs.read
needs fs.write
needs sys.read
needs screen.time
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

