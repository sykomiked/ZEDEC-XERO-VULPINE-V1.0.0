# ravelin — the exhaustive specification

| field | value |
|---|---|
| app id | `ravelin` |
| matrix layer | 333 |
| icon group | L3 |
| wave spec | `wave28/333-ravelin.md` |
| status | pending |

## 1. Purpose

protects critical sites such as power, water and data halls: physical access, cameras, alarms and continuity plans (`journal.query`, `log.tail`, `sys.info`, `features.get`, `files.read`, `files.write`). zxvd has no method for access control, camera and plant sensor feeds: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs journal`
- `needs sys.read`
- `needs notify`
- `needs fs.read`
- `needs fs.write`
- `needs net`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use pavise 1` — call `<id>.<entry>` after the use line
- `use damper 1` — call `<id>.<entry>` after the use line
- `use levin 1` — call `<id>.<entry>` after the use line
- `use weir 1` — call `<id>.<entry>` after the use line
- `use hedgerow 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident costs | primary | runs |
| FORTRAN | `trebago` | numeric | failure models | primary | runs |
| Ada | `eve` | limits | safety interlocks | primary | runs |
| APL | `kron` | arrays | zone sensor grid | primary | runs |
| MUMPS | `myra` | records | site asset tree | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id ravelin
matrix 333
name Ravelin
needs journal
needs sys.read
needs notify
needs fs.read
needs fs.write
needs net

provides difference num a b
provides product num a b
logic kron difference <<
0|_a-_b
.
logic kron product <<
0|_a*_b
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## 6. The entry specifications (what each entry computes)

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `interlock_ok` (a door, breaker or valve action allowed only with the site's interlocks satisfied and two people approving) in Ada as Eve + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `zones` sensor grid in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — `site_assets` tree in MUMPS as MYRA

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

