# cusp — the exhaustive specification

| field | value |
|---|---|
| app id | `cusp` |
| matrix layer | 287 |
| icon group | L3 |
| wave spec | `wave24/287-cusp.md` |
| status | pending |

## 1. Purpose

manages livestock: herd records, breeding, grazing rotation, vet visits and sales (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`, `place.list`).

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs ledger.post`
- `needs fs.read`
- `needs fs.write`
- `needs location`
- `needs net`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use sheaf 1` — call `<id>.<entry>` after the use line
- `use glebe 1` — call `<id>.<entry>` after the use line
- `use poultice 1` — call `<id>.<entry>` after the use line
- `use neap 1` — call `<id>.<entry>` after the use line
- `use kindred 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | livestock sales | primary | runs |
| FORTRAN | `trebago` | numeric | pasture growth model | primary | runs |
| Ada | `eve` | limits | medicine withdrawal limits | primary | runs |
| APL | `kron` | arrays | paddock grid | primary | runs |
| MUMPS | `myra` | records | animal pedigree tree | primary | runs |
| ABAP | `-` | business | business records | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id cusp
matrix 287
name Cusp
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs location
needs net
needs notify

provides modular_draw num seed members step
provides product num a b
provides difference num a b
logic kron modular_draw <<
0|(((_seed*1103515245)+12345)+(_step*7))|_members
.
logic kron product <<
0|_a*_b
.
logic kron difference <<
0|_a-_b
.
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
```

## 6. The entry specifications (what each entry computes)

### `modular_draw` (num) — kron

**Args.** `seed members step`

**Semantics.** The sortition's modular walk. Integer only, no hashing in the block. The residue operator is '|' in K ('%' is division).

**The exact check.** `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `paddocks` grid in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `pedigree` tree in MUMPS as MYRA

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

