# heddle — the exhaustive specification

| field | value |
|---|---|
| app id | `heddle` |
| matrix layer | 162 |
| icon group | L2 |
| wave spec | `wave13/162-heddle.md` |
| status | pending |

## 1. Purpose

designs woven and printed patterns and sends them to printers (`print.status`, `print.submit`, `print.job`) or looms as exact instructions, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for loom output: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs print`
- `needs fs.read`
- `needs fs.write`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use burin 1` — call `<id>.<entry>` after the use line
- `use platen 1` — call `<id>.<entry>` after the use line
- `use dyer 1` — call `<id>.<entry>` after the use line
- `use furrow 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | yarn and cost sheet | primary | runs |
| FORTRAN | `trebago` | numeric | tension math | primary | runs |
| Ada | `eve` | limits | machine limits | primary | runs |
| APL | `kron` | arrays | weave matrix | primary | runs |
| MUMPS | `myra` | records | pattern tree | primary | runs |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |
| Perl | `-` | pattern | patterns | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id heddle
matrix 162
name Heddle
needs print
needs fs.read
needs fs.write

provides modular_draw num seed members step
provides product num a b
provides count_ok truth count cap
logic kron modular_draw <<
0|(((_seed*1103515245)+12345)+(_step*7))|_members
.
logic kron product <<
0|_a*_b
.
logic eve count_ok <<
eve heddle
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
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

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — ends (ends per 10 cm x width) and picks exact in FORTRAN as Trebago

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — pattern tree in MUMPS as MYRA

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

