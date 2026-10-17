# viaduct — the exhaustive specification

| field | value |
|---|---|
| app id | `viaduct` |
| matrix layer | 327 |
| icon group | L3 |
| wave spec | `wave27/327-viaduct.md` |
| status | pending |

## 1. Purpose

settles payments between currencies, co-ops and networks with fair rates and clear records (`ledger.accounts`, `ledger.post`, `ledger.hold`, `ledger.release`, `netting.open`, `netting.positions`, `netting.close`, `swap.quote`).

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs ledger.post`
- `needs peers`
- `needs net`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use shallows 1` — call `<id>.<entry>` after the use line
- `use neap 1` — call `<id>.<entry>` after the use line
- `use mercer 1` — call `<id>.<entry>` after the use line
- `use trickle 1` — call `<id>.<entry>` after the use line
- `use coffer 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | settlement ledger | primary | runs |
| FORTRAN | `trebago` | numeric | exchange rate model | primary | runs |
| Ada | `eve` | limits | transfer limits | primary | runs |
| APL | `kron` | arrays | currency grid | primary | runs |
| MUMPS | `myra` | records | counterparty tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id viaduct
matrix 327
name Viaduct
needs ledger.read
needs ledger.post
needs peers
needs net
needs notify

provides limit_excess num value limit
provides product num a b
provides ratio_x1000 num num den
provides count_ok truth count cap
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron product <<
0|_a*_b
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic eve count_ok <<
eve viaduct
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `settlement_ledger` (in minor units, refused on overflow) in COBOL as Sutra

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `currencies` grid in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

