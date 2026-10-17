# stoker — the exhaustive specification

| field | value |
|---|---|
| app id | `stoker` |
| matrix layer | 196 |
| icon group | L2 |
| wave spec | `wave16/196-stoker.md` |
| status | pending |

## 1. Purpose

runs models on the fastest hardware present, GPU first: picks the backend, places weights in video memory, checks each model's signature and licence (`model.info`), and reports speed per watt (`power.status`, `power.top`, `sys.info`, `swarm.state`). zxvd has no method for GPU and backend enumeration, video-memory placement: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs ai.ask`
- `needs sys.read`
- `needs swarm`
- `needs fs.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use granary 1` — call `<id>.<entry>` after the use line
- `use retort 1` — call `<id>.<entry>` after the use line
- `use temper 1` — call `<id>.<entry>` after the use line
- `use brazier 1` — call `<id>.<entry>` after the use line
- `use hatchery 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | model admission log | primary | runs |
| FORTRAN | `trebago` | numeric | integer kernels | primary | runs |
| Ada | `eve` | limits | refuses unsigned or non-commercial weights | primary | runs |
| APL | `kron` | arrays | tensor layouts | primary | runs |
| MUMPS | `myra` | records | model catalogue | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| B | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id stoker
matrix 196
name Stoker
needs ai.ask
needs sys.read
needs swarm
needs fs.read

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

### 7.1 — "admit this model" (signed AND licence allows commercial use) in Ada as Eve + APL as KRON witnessed, refusing unsigned or non-commercial weights

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — tokens per second per watt (tokens x 10^6 / microjoules) in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — layers placed in video memory (largest prefix of layer bytes that fits the free bytes) in APL as KRON + FORTRAN as Trebago witnessed

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — model catalogue in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

