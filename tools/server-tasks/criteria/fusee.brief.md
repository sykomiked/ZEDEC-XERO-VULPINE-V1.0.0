# fusee — the complete brief

| | |
|---|---|
| matrix layer | 247 |
| icon group | L2 |
| wave spec | `wave20/247-fusee.md` |

## Purpose

Fronts the boot chain: the UEFI stubs (kernel/arch/x86_64/efi_stub.c, kernel/arch/arm64/efi_stub.c, kernel/arch/efi_models.h), measured boot (kernel/src/bootattest), boot features (kernel/src/bootfeat), A/B slots (kernel/src/loader/abupdate.c), update checks (kernel/src/update, zx_upcheck.c) and hardware keys (kernel/src/hwkey). Signed update check (`update.check`), the journal's chain (`journal.verify`, `journal.query`), switches (`features.get`), the machine (`sys.info`). Secure Boot keys, boot entries, A/B slot selection, firmware capsules, microcode and reading the measured-boot log have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `journal`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | boot-entry and slot ledger | primary | runs |
| FORTRAN | `trebago` | numeric | firmware version arithmetic | primary | runs |
| Ada | `eve` | limits | refuses unsigned stubs and rollback below the floor | primary | runs |
| APL | `kron` | arrays | measurement table | primary | runs |
| MUMPS | `myra` | records | firmware inventory tree | primary | runs |
| Nim | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Forth | `forth` | stack | small interpreters | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Nim): see docs/LANGUAGE_CATALOGUE.md for this tier
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.

## The spec's Logic clauses (what each component computes)

- - (critical) "may boot this image" = both signature checks pass AND the stub is signed AND the version is at or above the rollback floor, in Ada as Eve + APL as KRON witnessed
- - (critical) slot header check at boot (magic, version, tries left) in Forth (twin Cairndrake) + Ada as Eve witnessed
- - firmware version arithmetic (major.minor.patch packed into one integer, distance to the floor) exact in FORTRAN as Trebago
- - measurement table (expected and logged digest indexes, the first mismatch) in APL as KRON
- - boot-entry and slot ledger (tries counters per slot, refusing on overflow) in COBOL as Sutra
- - firmware inventory tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): one signature check passes and the other fails on the same image, giving BOTH
- the test asserts state 3 and that the image is never booted until the owner rules. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id fusee
matrix 247
name Fusee
needs sys.read
needs journal
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

