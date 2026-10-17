# plinth — the complete brief

| | |
|---|---|
| matrix layer | 254 |
| icon group | L2 |
| wave spec | `wave21/254-plinth.md` |

## Purpose

Fronts installation (install/install_zxv.py and install/transactional_disk.py, host only today; build_system/mk_golden_disc.sh; kernel/src/blockdev; kernel/src/zxvfs; kernel/src/loader/abupdate.c for the A/B slots; kernel/src/hwkey for sealing). Room and discs (`sys.disk`, `sys.info`), the backup check (`backup.verify`), the signed image check (`update.check`). Listing block devices, partitioning, formatting and sealing a key to the machine have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `backup`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | install transaction log | primary | runs |
| FORTRAN | `trebago` | numeric | partition sizing | primary | runs |
| Ada | `eve` | limits | refuses to touch a disc that is not blank or not confirmed | primary | runs |
| APL | `kron` | arrays | block maps | primary | runs |
| MUMPS | `myra` | records | partition table records | primary | runs |
| Zig | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Zig): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- - (critical) "may write this disc" = blank by both probes AND the owner typed this disc's id to confirm AND it is not the disc the system runs from, in Ada as Eve + APL as KRON witnessed
- - (critical) partition sizes (ESP, system A, system B, models, encrypted data
- 1 MiB aligned
- the sum within the disc) in FORTRAN as Trebago + APL as KRON witnessed
- - block maps (partitions x start, size) in APL as KRON
- - install transaction log (steps with byte counts, refusing on overflow) in COBOL as Sutra
- - partition table records in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): probe A finds no partition table (blank) and probe B finds a filesystem signature, giving BOTH
- the test, on an image file, asserts state 3 and that the disc is never formatted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id plinth
matrix 254
name Plinth
needs sys.read
needs fs.read
needs fs.write
needs backup
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

