# decanter — the exhaustive specification

| field | value |
|---|---|
| app id | `decanter` |
| matrix layer | 232 |
| icon group | L2 |
| wave spec | `wave19/232-decanter.md` |
| status | pending |

## 1. Purpose

Fronts the Vino engines (kernel/src/vino, kernel/src/vino_stores, kernel/src/airc for relay time, kernel/src/ipfs_node for storage) as a router: Vino held and quoted (`swap.status`, `swap.quote`, `ledger.balance`), held while a route runs (`ledger.hold`, `ledger.holds`, `ledger.release`), compute from another node (`peer.ask`, `peer.status`), storage (`aipi.put`, `aipi.status`), energy (`power.budget`), switches (`features.get`). Buying AIRC relay time, a storage route and a route list have no method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs ledger.post`
- `needs peers`
- `needs net`
- `needs swarm`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use vintner 1` — call `<id>.<entry>` after the use line
- `use forage 1` — call `<id>.<entry>` after the use line
- `use coffer 1` — call `<id>.<entry>` after the use line
- `use seedbank 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | Vino ledger | primary | runs |
| FORTRAN | `trebago` | numeric | exchange arithmetic | primary | runs |
| Ada | `eve` | limits | per-route limits | primary | runs |
| APL | `kron` | arrays | route table | primary | runs |
| MUMPS | `myra` | records | holder records | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Ruby | `ruby` | scripting | scripting | primary | planned: SDK host only, no in-app runtime yet |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id decanter
matrix 232
name Decanter
needs ledger.read
needs ledger.post
needs peers
needs net
needs swarm
needs notify

provides ratio_x1000 num num den
provides limit_excess num value limit
provides composite_key num high low
provides running_state truth state event
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

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

### 7.1 — - Vino ledger lines refusing on overflow in COBOL as Sutra

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — Paraconsistent (BOTH test, in test_zxapp.c): the provider reports 3600 relay seconds delivered and the holder's meter 1800, giving BOTH

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — never re-derive the tables

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

