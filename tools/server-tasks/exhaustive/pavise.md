# pavise — the exhaustive specification

| field | value |
|---|---|
| app id | `pavise` |
| matrix layer | 153 |
| icon group | L2 |
| wave spec | `wave12/153-pavise.md` |
| status | pending |

## 1. Purpose

one security perimeter combining firewall paths, app permissions and keys into a single policy, read from `features.get`, `sys.net`, `net.peers`, `devmesh.devices`, `passkeys.list`, with its history from `journal.query` and `journal.verify`. zxvd has no method for firewall rule write, capability grant write: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs net`
- `needs peers`
- `needs journal`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use gatehouse 1` — call `<id>.<entry>` after the use line
- `use lockbox 1` — call `<id>.<entry>` after the use line
- `use witness 1` — call `<id>.<entry>` after the use line
- `use hedgerow 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | access ledger | primary | runs |
| FORTRAN | `trebago` | numeric | threat scoring | primary | runs |
| Ada | `eve` | limits | declared policy bounds | primary | runs |
| APL | `kron` | arrays | permission matrix | primary | runs |
| MUMPS | `myra` | records | trust tree | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id pavise
matrix 153
name Pavise
needs sys.read
needs net
needs peers
needs journal
needs notify

provides composite_key num high low
provides limit_excess num value limit
provides flags_none truth bits forbidden
provides clamp num value lo hi
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron flags_none <<
(_bits&_forbidden)=0
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
```

## 6. The entry specifications (what each entry computes)

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `['check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000', 'check limit_excess value=1000,999 limit=1000,1000 = 0,0', 'check limit_excess value=1,1000000 limit=0,999999 = 1,1']`

### `flags_none` (truth) — kron

**Args.** `bits forbidden`

**Semantics.** No forbidden bit set: (bits & forbidden) == 0. A truth entry.

**The exact check.** `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false`

### `clamp` (num) — kron

**Args.** `value lo hi`

**Semantics.** The value clamped into [lo,hi]. The result is always within the bounds.

**The exact check.** `check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — trust tree in MUMPS as MYRA

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

