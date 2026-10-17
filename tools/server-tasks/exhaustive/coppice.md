# coppice — the exhaustive specification

| field | value |
|---|---|
| app id | `coppice` |
| matrix layer | 192 |
| icon group | L2 |
| wave spec | `wave15/192-coppice.md` |
| status | pending |

## 1. Purpose

manages woodland: tree inventory, growth, felling plans and carbon held, with places (`place.list`, `place.add`) and records in the Hoard (`files.read`, `files.write`).

## 2. Capabilities (the exact registry names and scopes)

- `needs location`
- `needs fs.read`
- `needs fs.write`
- `needs ledger.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use gleaner 1` — call `<id>.<entry>` after the use line
- `use ninefold 1` — call `<id>.<entry>` after the use line
- `use waymark 1` — call `<id>.<entry>` after the use line
- `use understory 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | timber and carbon accounts | primary | runs |
| FORTRAN | `trebago` | numeric | growth model | primary | runs |
| Ada | `eve` | limits | felling limits | primary | runs |
| APL | `kron` | arrays | stand grid | primary | runs |
| MUMPS | `myra` | records | tree inventory | primary | runs |
| Scheme | `scheme` | symbolic | symbolic reasoning | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id coppice
matrix 192
name Coppice
needs location
needs fs.read
needs fs.write
needs ledger.read

provides within_range truth value lo hi
provides count_ok truth count cap
logic eve within_range <<
eve coppice
values = 0 .. 100

within_range {
  value: values 0 .. 100
  lo: values 0 .. 0
  hi: values 100 .. 100
}
.
logic eve count_ok <<
eve coppice
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## 6. The entry specifications (what each entry computes)

### `within_range` (truth) — eve

**Args.** `value lo hi`

**Semantics.** The range law. true when lo <= value <= hi, false outside. Both bounds inclusive. A truth entry: the four values apply (a contradicted claim is both, no evidence is neither).

**The exact check.** `check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — diameter growth per year in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — stand grid in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — tree inventory in MUMPS as MYRA

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

