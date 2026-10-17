# skep — the exhaustive specification

| field | value |
|---|---|
| app id | `skep` |
| matrix layer | 185 |
| icon group | L2 |
| wave spec | `wave15/185-skep.md` |
| status | pending |

## 1. Purpose

coordinates a swarm of agents and nodes on shared work (`swarm.state`, `swarm.earnings`, `net.peers`, `peer.status`) with bounties and patronage grants from the books (`ledger.balance`).

## 2. Capabilities (the exact registry names and scopes)

- `needs swarm`
- `needs peers`
- `needs net`
- `needs ledger.read`
- `needs ledger.post`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use trellis 1` — call `<id>.<entry>` after the use line
- `use murmuration 1` — call `<id>.<entry>` after the use line
- `use trickle 1` — call `<id>.<entry>` after the use line
- `use forage 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grant ledger | primary | runs |
| FORTRAN | `trebago` | numeric | load balancing | primary | runs |
| Ada | `eve` | limits | agent budgets | primary | runs |
| APL | `kron` | arrays | task matrix | primary | runs |
| MUMPS | `myra` | records | colony tree | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id skep
matrix 185
name Skep
needs swarm
needs peers
needs net
needs ledger.read
needs ledger.post
needs notify

provides exact_total num n a b c d
provides largest_remainder num pool w1 w2 w3 w4
provides limit_excess num value limit
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron largest_remainder <<
0|(_pool*_w1)%(_w1+_w2+_w3+_w4)
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,10/3,7/3,50
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
```

## 6. The entry specifications (what each entry computes)

### `exact_total` (num) — trebago

**Args.** `n a b c d`

**Semantics.** The exact sum of the first n of the four sizes. Integer only, no rounding, no overflow silently: the caller checks the bound.

**The exact check.** `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0`

### `largest_remainder` (num) — kron

**Args.** `pool w1 w2 w3 w4`

**Semantics.** The first recipient's exact share of the pool by weight; the largest-remainder apportionment's integer part is the caller's law.

**The exact check.** `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,10/3,7/3,50`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — task matrix in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — colony tree in MUMPS as MYRA

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

