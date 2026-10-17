# armature — the exhaustive specification

| field | value |
|---|---|
| app id | `armature` |
| matrix layer | 297 |
| icon group | L3 |
| wave spec | `wave25/297-armature.md` |
| status | pending |

## 1. Purpose

an invention lab notebook: experiments, prototypes, parts, results and dated proof of who invented what (`files.read`, `files.write`, `journal.query`, `web4.cid`, `aipi.put`).

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs journal`
- `needs sys.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use ratchet 1` — call `<id>.<entry>` after the use line
- `use cupel 1` — call `<id>.<entry>` after the use line
- `use cartulary 1` — call `<id>.<entry>` after the use line
- `use graft 1` — call `<id>.<entry>` after the use line
- `use lapidary 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | prototype costs | primary | runs |
| FORTRAN | `trebago` | numeric | test calculations | primary | runs |
| Ada | `eve` | limits | safety limits | primary | runs |
| APL | `kron` | arrays | result matrix | primary | runs |
| MUMPS | `myra` | records | design revision tree | primary | runs |
| SNOBOL | `-` | text | text | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id armature
matrix 297
name Armature
needs fs.read
needs fs.write
needs journal
needs sys.read

provides ratio_x1000 num num den
provides product num a b
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron product <<
0|_a*_b
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## 6. The entry specifications (what each entry computes)

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `proof_ok` (an entry is dated proof only when its content id is on the journal and the chain is intact) in Ada as Eve + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `results` matrix in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — `revisions` tree in MUMPS as MYRA

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

