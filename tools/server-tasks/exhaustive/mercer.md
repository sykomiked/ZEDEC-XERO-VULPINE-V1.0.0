# mercer — the exhaustive specification

| field | value |
|---|---|
| app id | `mercer` |
| matrix layer | 171 |
| icon group | L2 |
| wave spec | `wave14/171-mercer.md` |
| status | pending |

## 1. Purpose

runs a small shop: stock, prices, sales and receipts in the books (`ledger.post`, `ledger.balance`, `ledger.entries`) and the Hoard (`files.read`, `files.write`), receipts printed by `print.submit`, market listings from `net.offers`, the seller's card from `player.card`.

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs ledger.post`
- `needs fs.read`
- `needs fs.write`
- `needs print`
- `needs net`
- `needs peers`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use coffer 1` — call `<id>.<entry>` after the use line
- `use tallystick 1` — call `<id>.<entry>` after the use line
- `use fairground 1` — call `<id>.<entry>` after the use line
- `use tally 1` — call `<id>.<entry>` after the use line
- `use talisman 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | sales ledger | primary | runs |
| FORTRAN | `trebago` | numeric | pricing model | primary | runs |
| Ada | `eve` | limits | credit limits | primary | runs |
| APL | `kron` | arrays | stock grid | primary | runs |
| MUMPS | `myra` | records | product tree | primary | runs |
| LISP | `lisp` | lists | lists | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id mercer
matrix 171
name Mercer
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs print
needs net
needs peers

provides product num a b
provides running_state truth state event
provides limit_excess num value limit
logic kron product <<
0|_a*_b
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
```

## 6. The entry specifications (what each entry computes)

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — stock grid in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

