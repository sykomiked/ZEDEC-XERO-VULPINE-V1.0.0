# cupel — the exhaustive specification

| field | value |
|---|---|
| app id | `cupel` |
| matrix layer | 179 |
| icon group | L2 |
| wave spec | `wave14/179-cupel.md` |
| status | pending |

## 1. Purpose

simulates atoms, molecules and physical systems on the integer engine and compares models, runs kept in the Hoard (`files.read`, `files.write`), machine facts from `sys.info`.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs sys.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use furrow 1` — call `<id>.<entry>` after the use line
- `use relief 1` — call `<id>.<entry>` after the use line
- `use retort 1` — call `<id>.<entry>` after the use line
- `use gnomon 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | run records | primary | runs |
| FORTRAN | `trebago` | numeric | simulation core | primary | runs |
| Ada | `eve` | limits | precision bounds | primary | runs |
| APL | `kron` | arrays | lattice arrays | primary | runs |
| MUMPS | `myra` | records | model tree | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id cupel
matrix 179
name Cupel
needs fs.read
needs fs.write
needs sys.read

provides count_ok truth count cap
provides clamp num value lo hi
provides composite_key num high low
logic eve count_ok <<
eve cupel
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `['check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false', 'check count_ok count=2 cap=100 = true', 'check count_ok count=99 cap=100 = true', 'check count_ok count=101 cap=100 = false', 'check count_ok count=100 cap=100 = true', 'check count_ok count=0 cap=100 = false', 'check count_ok count=1 cap=100 = true']`

### `clamp` (num) — kron

**Args.** `value lo hi`

**Semantics.** The value clamped into [lo,hi]. The result is always within the bounds.

**The exact check.** `check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — one integer leapfrog step (fixed-point positions and velocities, forces from a pair table) in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — lattice arrays in APL as KRON

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

