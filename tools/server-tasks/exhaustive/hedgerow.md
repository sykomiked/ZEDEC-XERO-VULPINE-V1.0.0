# hedgerow — the exhaustive specification

| field | value |
|---|---|
| app id | `hedgerow` |
| matrix layer | 30 |
| icon group | L1 |
| wave spec | `wave2/030-hedgerow.md` |
| status | pending |

## 1. Purpose

who may reach this machine: network mode (`net.command` off/lan/online), answering peers (`peer.serve`), the phone link (`phone.link`, `phone.list`), switches that expose methods (`features.get`), hosted spaces (`pspace.list`) and the IRC bridge (`irc.status`). Packet filtering and per-port rules have no method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs net`
- `needs peers`
- `needs sys.read`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use coffer 1` — call `<id>.<entry>` after the use line
- `use gatehouse 1` — call `<id>.<entry>` after the use line
- `use vintner 1` — call `<id>.<entry>` after the use line
- `use ninefold 1` — call `<id>.<entry>` after the use line
- `use kitling 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| HyperTalk | `-` | view | view and model | primary | planned: needs Lingua front-end |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id hedgerow
matrix 30
name Hedgerow
needs net
needs peers
needs sys.read
needs notify

provides composite_key num high low
logic kron composite_key <<
0|((_high*1000)+_low)
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## 6. The entry specifications (what each entry computes)

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — "is this path reachable" (mode, switch, private bind) in eve + kron + C++ wasm (three witnesses)

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — address class (loopback, private, link-local, unique-local, from octets) in Rust wasm + kron witnessed

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

### 7.3 — Opening anything is asked first

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic myra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`memo.myra`):**

```
^ZXBIRDSONG(memo:i9) = rate:i5! ^ channels:i1! ^ frames:i9! ^ mtime:i12!
```

### 7.4 — closing is at once

**Runtime.** `-` — 

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic - <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

