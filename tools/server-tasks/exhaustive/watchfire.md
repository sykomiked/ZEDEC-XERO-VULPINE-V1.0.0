# watchfire — the exhaustive specification

| field | value |
|---|---|
| app id | `watchfire` |
| matrix layer | 143 |
| icon group | L1 |
| wave spec | `wave11/143-watchfire.md` |
| status | pending |

## 1. Purpose

emergency contacts and a medical ID shown on the lock screen; on the person's request, ring contacts (`call.start`, signalling only) and share where the machine is (`place.ask`, `place.here`). Contacts' devices from `phone.list`.

## 2. Capabilities (the exact registry names and scopes)

- `needs location`
- `needs peers`
- `needs notify`
- `needs fs.read`
- `needs fs.write`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- none beyond the core (layer 0)

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Swift | `swift` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id watchfire
matrix 143
name Watchfire
needs location
needs peers
needs notify
needs fs.read
needs fs.write

provides share_x100 num part whole
provides composite_key num high low
logic kron share_x100 <<
0|(_part*10000)%_whole
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## 6. The entry specifications (what each entry computes)

### `share_x100` (num) — kron

**Args.** `part whole`

**Semantics.** A part's share of a whole in hundredths of a percent, EXACT rational (the K's division is exact: 10000/3, not 3333). The caller floors.

**The exact check.** `['check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000', 'check share_x100 part=0 whole=1 = 0', 'check share_x100 part=1 whole=1 = 10000', 'check share_x100 part=1 whole=10000 = 1', 'check share_x100 part=3 whole=4 = 7500', 'check share_x100 part=2 whole=3 = 20000/3', 'check share_x100 part=1 whole=2 = 5000']`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — cancel window before any ring (10..60 s, never skipped) in eve + kron witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — Life-safety: advisory and human-approved

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`earned.eve`):**

```
eve talisman
margin = 0 .. 999999999999999999
uptime_margin = 0 .. 10000

tier_earned {
  cred: margin
  bond: margin
  uptime: uptime_margin
}
```

### 7.3 — never calls emergency services or anyone by itself

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic myra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`memo.myra`):**

```
^ZXBIRDSONG(memo:i9) = rate:i5! ^ channels:i1! ^ frames:i9! ^ mtime:i12!
```

### 7.4 — not a medical device

**Runtime.** `-` — 

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic - <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.5 — nothing clinical is inferred

**Runtime.** `swift` — 

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic swift <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

