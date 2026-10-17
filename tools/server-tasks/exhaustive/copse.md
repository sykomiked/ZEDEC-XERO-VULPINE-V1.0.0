# copse — the exhaustive specification

| field | value |
|---|---|
| app id | `copse` |
| matrix layer | 235 |
| icon group | L2 |
| wave spec | `wave19/235-copse.md` |
| status | pending |

## 1. Purpose

Plans woodworking: designs, cut lists, joints, materials and cost, kept in the Hoard (`files.read`, `files.write`) and printed (`print.submit`).

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs print`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use relief 1` — call `<id>.<entry>` after the use line
- `use tally 1` — call `<id>.<entry>` after the use line
- `use gnomon 1` — call `<id>.<entry>` after the use line
- `use bowline 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | material costs | primary | runs |
| FORTRAN | `trebago` | numeric | board yield | primary | runs |
| Ada | `eve` | limits | tolerance limits | primary | runs |
| APL | `kron` | arrays | cut grid | primary | runs |
| MUMPS | `myra` | records | assembly tree | primary | runs |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| LISP | `lisp` | lists | lists | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id copse
matrix 235
name Copse
needs fs.read
needs fs.write
needs print

provides count_ok truth count cap
provides difference num a b
provides within_range truth value lo hi
provides product num a b
provides running_state truth state event
logic eve count_ok <<
eve copse
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron difference <<
0|_a-_b
.
logic eve within_range <<
eve copse
values = 0 .. 100

within_range {
  value: values 0 .. 100
  lo: values 0 .. 0
  hi: values 100 .. 100
}
.
logic kron product <<
0|_a*_b
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

### `within_range` (truth) — eve

**Args.** `value lo hi`

**Semantics.** The range law. true when lo <= value <= hi, false outside. Both bounds inclusive. A truth entry: the four values apply (a contradicted claim is both, no evidence is neither).

**The exact check.** `check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — - cut grid (boards x parts) in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — - assembly tree in MUMPS as MYRA

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — Paraconsistent (BOTH test, in test_zxapp.c): one board measured at 1203 mm and again at 1198 mm with a 2 mm tolerance gives BOTH

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — never re-derive the tables

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

