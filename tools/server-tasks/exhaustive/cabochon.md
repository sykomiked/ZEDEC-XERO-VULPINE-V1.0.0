# cabochon — the exhaustive specification

| field | value |
|---|---|
| app id | `cabochon` |
| matrix layer | 278 |
| icon group | L3 |
| wave spec | `wave23/278-cabochon.md` |
| status | pending |

## 1. Purpose

gathers accounts, savings goals, bills and investments into one honest picture of your wealth (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.holds`).

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs fs.read`
- `needs fs.write`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use neap 1` — call `<id>.<entry>` after the use line
- `use mercer 1` — call `<id>.<entry>` after the use line
- `use cartulary 1` — call `<id>.<entry>` after the use line
- `use coffer 1` — call `<id>.<entry>` after the use line
- `use larder 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | household books | primary | runs |
| FORTRAN | `trebago` | numeric | compound growth model | primary | runs |
| Ada | `eve` | limits | spending caps | primary | runs |
| APL | `kron` | arrays | portfolio grid | primary | runs |
| MUMPS | `myra` | records | family account tree | primary | runs |
| Objective-C | `-` | objects | domain objects | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id cabochon
matrix 278
name Cabochon
needs ledger.read
needs fs.read
needs fs.write
needs notify

provides difference num a b
provides ratio_x1000 num num den
provides limit_excess num value limit
provides count_ok truth count cap
logic kron difference <<
0|_a-_b
.
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
logic eve count_ok <<
eve cabochon
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## 6. The entry specifications (what each entry computes)

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `portfolio` grid in APL as KRON

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

