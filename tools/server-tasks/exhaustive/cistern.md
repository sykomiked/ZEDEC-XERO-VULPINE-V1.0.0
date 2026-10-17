# cistern — the exhaustive specification

| field | value |
|---|---|
| app id | `cistern` |
| matrix layer | 296 |
| icon group | L3 |
| wave spec | `wave25/296-cistern.md` |
| status | pending |

## 1. Purpose

runs a water utility: pipes, pumps, leaks, meter readings, quality tests and customer bills (`files.read`, `files.write`, `journal.query`, `ledger.entries`, `ledger.post`, `print.submit`). zxvd has no method for meter, pump and quality sensor readings: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs journal`
- `needs ledger.read`
- `needs ledger.post`
- `needs print`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use weir 1` — call `<id>.<entry>` after the use line
- `use millrace 1` — call `<id>.<entry>` after the use line
- `use shipwright 1` — call `<id>.<entry>` after the use line
- `use chainman 1` — call `<id>.<entry>` after the use line
- `use tallystick 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | customer billing | primary | runs |
| FORTRAN | `trebago` | numeric | hydraulic model | primary | runs |
| Ada | `eve` | limits | pressure and dosing limits | primary | runs |
| APL | `kron` | arrays | meter arrays | primary | runs |
| MUMPS | `myra` | records | network asset tree | primary | runs |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id cistern
matrix 296
name Cistern
needs fs.read
needs fs.write
needs journal
needs ledger.read
needs ledger.post
needs print
needs notify

provides grade_band num value b1 b2 b3 b4
provides difference num a b
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
logic kron difference <<
0|_a-_b
.
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
```

## 6. The entry specifications (what each entry computes)

### `grade_band` (num) — kron

**Args.** `value b1 b2 b3 b4`

**Semantics.** The band index 0..4 of the value across the four ascending bounds. The comparisons sum (each true adds 1).

**The exact check.** `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4`

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `hydraulic` (flow and head loss per pipe from the integer tables) in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `meters` arrays in APL as KRON

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

