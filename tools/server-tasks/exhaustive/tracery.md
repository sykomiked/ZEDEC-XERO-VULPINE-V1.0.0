# tracery — the exhaustive specification

| field | value |
|---|---|
| app id | `tracery` |
| matrix layer | 275 |
| icon group | L3 |
| wave spec | `wave23/275-tracery.md` |
| status | pending |

## 1. Purpose

records wildlife sightings, herd counts and habitat health across a reserve (`place.here`, `place.list`, `place.add`, `files.read`, `files.write`, `search.query`) for rangers and landowners.

## 2. Capabilities (the exact registry names and scopes)

- `needs location`
- `needs fs.read`
- `needs fs.write`
- `needs search`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use coppice 1` — call `<id>.<entry>` after the use line
- `use verderer 1` — call `<id>.<entry>` after the use line
- `use chainman 1` — call `<id>.<entry>` after the use line
- `use fen 1` — call `<id>.<entry>` after the use line
- `use understory 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | permit fees | primary | runs |
| FORTRAN | `trebago` | numeric | population model | primary | runs |
| Ada | `eve` | limits | cull and quota limits | primary | runs |
| APL | `kron` | arrays | sighting grid | primary | runs |
| MUMPS | `myra` | records | species tree | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id tracery
matrix 275
name Tracery
needs location
needs fs.read
needs fs.write
needs search

provides count_ok truth count cap
provides product num a b
logic eve count_ok <<
eve tracery
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron product <<
0|_a*_b
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `sightings` grid in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `species` tree in MUMPS as MYRA

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

