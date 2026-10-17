# spire — the exhaustive specification

| field | value |
|---|---|
| app id | `spire` |
| matrix layer | 323 |
| icon group | L3 |
| wave spec | `wave27/323-spire.md` |
| status | pending |

## 1. Purpose

manages geology and mining surveys: samples, claims, drill logs, reserves and land restoration (`files.read`, `files.write`, `search.query`, `place.list`, `aipi.execute`).

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs search`
- `needs location`
- `needs peers`
- `needs net`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use collier 1` — call `<id>.<entry>` after the use line
- `use massif 1` — call `<id>.<entry>` after the use line
- `use chainman 1` — call `<id>.<entry>` after the use line
- `use cupel 1` — call `<id>.<entry>` after the use line
- `use understory 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | royalty accounts | primary | runs |
| FORTRAN | `trebago` | numeric | ore body model | primary | runs |
| Ada | `eve` | limits | extraction limits | primary | runs |
| APL | `kron` | arrays | drill grid | primary | runs |
| MUMPS | `myra` | records | claim hierarchy | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id spire
matrix 323
name Spire
needs fs.read
needs fs.write
needs search
needs location
needs peers
needs net

provides count_ok truth count cap
provides grade_band num value b1 b2 b3 b4
logic eve count_ok <<
eve spire
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `grade_band` (num) — kron

**Args.** `value b1 b2 b3 b4`

**Semantics.** The band index 0..4 of the value across the four ascending bounds. The comparisons sum (each true adds 1).

**The exact check.** `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `drill` grid in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `claims` hierarchy in MUMPS as MYRA

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

