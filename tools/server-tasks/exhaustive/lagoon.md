# lagoon — the exhaustive specification

| field | value |
|---|---|
| app id | `lagoon` |
| matrix layer | 270 |
| icon group | L2 |
| wave spec | `wave22/270-lagoon.md` |
| status | pending |

## 1. Purpose

Holds savings and guardianship plans for children until they come of age: accounts (`ledger.open_account`, `ledger.balance`, `ledger.entries`, `ledger.post`), escrow (`ledger.hold`, `ledger.holds`, `ledger.release`), the estate plan and guardians (`estate.status`, `recovery.status`), the child's seat (`seats.list`).

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs ledger.post`
- `needs screen.time`
- `needs sys.read`
- `needs peers`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use heirloom 1` — call `<id>.<entry>` after the use line
- `use coffer 1` — call `<id>.<entry>` after the use line
- `use cubby 1` — call `<id>.<entry>` after the use line
- `use shepherd 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | trust accounts | primary | runs |
| FORTRAN | `trebago` | numeric | growth model | primary | runs |
| Ada | `eve` | limits | withdrawal limits | primary | runs |
| APL | `kron` | arrays | beneficiary grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id lagoon
matrix 270
name Lagoon
needs ledger.read
needs ledger.post
needs screen.time
needs sys.read
needs peers
needs notify

provides limit_excess num value limit
provides modular_draw num seed members step
provides count_ok truth count cap
provides exact_total num n a b c d
provides ratio_x1000 num num den
provides product num a b
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
logic eve count_ok <<
eve lagoon
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron product <<
0|_a*_b
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
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

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `['check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false', 'check count_ok count=2 cap=100 = true', 'check count_ok count=99 cap=100 = true', 'check count_ok count=101 cap=100 = false', 'check count_ok count=100 cap=100 = true', 'check count_ok count=0 cap=100 = false', 'check count_ok count=1 cap=100 = true']`

### `exact_total` (num) — trebago

**Args.** `n a b c d`

**Semantics.** The exact sum of the first n of the four sizes. Integer only, no rounding, no overflow silently: the caller checks the bound.

**The exact check.** `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0`

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — - family tree in MUMPS as MYRA

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — - a child's savings lessons, one step at a time, in BASIC (twin Hatchling)

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

### 7.3 — - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

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

### 7.4 — never re-derive the tables

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

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

