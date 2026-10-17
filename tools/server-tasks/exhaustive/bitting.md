# bitting — the exhaustive specification

| field | value |
|---|---|
| app id | `bitting` |
| matrix layer | 307 |
| icon group | L3 |
| wave spec | `wave25/307-bitting.md` |
| status | pending |

## 1. Purpose

issues and revokes digital keys for doors, rooms, vehicles and shared equipment, with a full access history (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `journal.query`). zxvd has no method for door and vehicle lock control: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs peers`
- `needs journal`
- `needs notify`
- `needs ledger.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use warding 1` — call `<id>.<entry>` after the use line
- `use moraine 1` — call `<id>.<entry>` after the use line
- `use pavise 1` — call `<id>.<entry>` after the use line
- `use lockbox 1` — call `<id>.<entry>` after the use line
- `use gatehouse 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | key deposits | primary | runs |
| FORTRAN | `trebago` | numeric | access pattern scoring | primary | runs |
| Ada | `eve` | limits | time-window limits | primary | runs |
| APL | `kron` | arrays | door permission grid | primary | runs |
| MUMPS | `myra` | records | holder tree | primary | runs |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id bitting
matrix 307
name Bitting
needs sys.read
needs peers
needs journal
needs notify
needs ledger.read

provides composite_key num high low
provides flags_none truth bits forbidden
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron flags_none <<
(_bits&_forbidden)=0
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `flags_none` (truth) — kron

**Args.** `bits forbidden`

**Semantics.** No forbidden bit set: (bits & forbidden) == 0. A truth entry.

**The exact check.** `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `access_pattern` (uses per hour against the holder's usual pattern, integer score) in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `holders` tree in MUMPS as MYRA

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

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

