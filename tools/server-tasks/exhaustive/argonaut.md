# argonaut — the exhaustive specification

| field | value |
|---|---|
| app id | `argonaut` |
| matrix layer | 367 |
| icon group | L3 |
| wave spec | `wave30/367-argonaut.md` |
| status | pending |

## 1. Purpose

Grows savings in chambered goals that compound over time, with locked portions: accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.open_account`, `ledger.post`), holds for locked chambers (`ledger.hold`, `ledger.holds`, `ledger.release`).

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs ledger.post`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use neap 1` — call `<id>.<entry>` after the use line
- `use eddy 1` — call `<id>.<entry>` after the use line
- `use holm 1` — call `<id>.<entry>` after the use line
- `use mercer 1` — call `<id>.<entry>` after the use line
- `use larder 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | savings ledger | primary | runs |
| FORTRAN | `trebago` | numeric | integer compound interest | primary | runs |
| Ada | `eve` | limits | withdrawal limits | primary | runs |
| APL | `kron` | arrays | goal grid | primary | runs |
| MUMPS | `myra` | records | household tree | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id argonaut
matrix 367
name Argonaut
needs ledger.read
needs ledger.post
needs notify

provides limit_excess num value limit
provides modular_draw num seed members step
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
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `['check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000', 'check limit_excess value=1000,999 limit=1000,1000 = 0,0', 'check limit_excess value=1,1000000 limit=0,999999 = 1,1']`

### `modular_draw` (num) — kron

**Args.** `seed members step`

**Semantics.** The sortition's modular walk. Integer only, no hashing in the block. The residue operator is '|' in K ('%' is division).

**The exact check.** `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — a future chamber only after its date) in Ada as Eve + COBOL as Sutra witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — - `compound`: (critical) integer compound growth per chamber (balance x (den + num) / den per period, floor, remainder carried) in FORTRAN as Trebago + COBOL as Sutra witnessed

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`used.sutra`):**

```
record used-share
  total: 9(18)
  left: 9(18)
  used: 9(5)
procedure
  compute used = (total - left) * 10000 / total
  answer used
```

### 7.3 — - `ledger`: savings ledger (minor units, refusing on overflow) in COBOL as Sutra

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`count.tb`):**

```
trebago kind_count
  in  kind(n), want
  out count
  count = sum(i, max(0, min(1, 1 + kind(i) - want)) * max(0, min(1, 1 + want - kind(i))))
end
```

### 7.4 — - `goals`: goal grid (chambers x months) in APL as KRON

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`transfer.eve`):**

```
eve coffer
minor_amount = 1 .. 9007199254740991
minor_left = 0 .. 999999999999999999
minor_gap = 0 .. 0

transfer_rule {
  amount: minor_amount
  after: minor_left
  room: minor_left
  gap: minor_gap
}
```

### 7.5 — - `household`: household tree in MUMPS as MYRA

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic kron <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`use.kron`):**

```
(asked>0)&(granted>0)&revoked<1
```

### 7.6 — never re-derive the tables

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic myra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`dock.myra`):**

```
^ZXDOCK(place:i2) = app:i3! ^ pinned:i1! ^ added:i12!
```

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

