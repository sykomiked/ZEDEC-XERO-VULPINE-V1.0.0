# bulwark — the exhaustive specification

| field | value |
|---|---|
| app id | `bulwark` |
| matrix layer | 375 |
| icon group | L3 |
| wave spec | `wave31/375-bulwark.md` |
| status | pending |

## 1. Purpose

Protects an organisation's keys, assets and accounts with layered guardians, approvals and recovery: keys and devices (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`), guardians and recovery (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`), transfers held (`ledger.hold`, `ledger.holds`, `ledger.release`), the record (`journal.query`, `journal.verify`).

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs peers`
- `needs ledger.read`
- `needs ledger.post`
- `needs journal`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use pavise 1` — call `<id>.<entry>` after the use line
- `use warding 1` — call `<id>.<entry>` after the use line
- `use moraine 1` — call `<id>.<entry>` after the use line
- `use lockbox 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | asset register | primary | runs |
| FORTRAN | `trebago` | numeric | risk scoring | primary | runs |
| Ada | `eve` | limits | withdrawal limits | primary | runs |
| APL | `kron` | arrays | access matrix | primary | runs |
| MUMPS | `myra` | records | guardian tree | primary | runs |
| Smalltalk | `-` | objects | domain objects | primary | planned: needs Lingua front-end |
| Erlang | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id bulwark
matrix 375
name Bulwark
needs sys.read
needs peers
needs ledger.read
needs ledger.post
needs journal
needs notify

provides limit_excess num value limit
provides modular_draw num seed members step
provides composite_key num high low
provides running_state truth state event
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron modular_draw <<
0|(((_seed*1103515245)+12345)+(_step*7))|_members
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `modular_draw` (num) — kron

**Args.** `seed members step`

**Semantics.** The sortition's modular walk. Integer only, no hashing in the block. The residue operator is '|' in K ('%' is division).

**The exact check.** `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — - `access`: access matrix (people x assets) in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — - `register`: asset register (minor units, refusing on overflow) in COBOL as Sutra

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — - `roles`: role hierarchy and approval layers in Ada as Eve

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — - `guardians`: guardian tree in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.5 — Paraconsistent (BOTH test, in test_zxapp.c): two guardians approve and one vetoes the same transfer: it is BOTH

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic kron <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.6 — never re-derive the tables

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic myra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

