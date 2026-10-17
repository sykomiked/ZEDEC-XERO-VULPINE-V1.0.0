# topiary — the exhaustive specification

| field | value |
|---|---|
| app id | `topiary` |
| matrix layer | 204 |
| icon group | L2 |
| wave spec | `wave16/204-topiary.md` |
| status | pending |

## 1. Purpose

shapes habits over months with small daily tasks and reminders in the Hoard (`files.read`, `files.write`) and the companion's nudges (`companion.mood`, `swarm.ask`).

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs notify`
- `needs ai.ask`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use solstice 1` — call `<id>.<entry>` after the use line
- `use bowline 1` — call `<id>.<entry>` after the use line
- `use kitling 1` — call `<id>.<entry>` after the use line
- `use wellspring 1` — call `<id>.<entry>` after the use line
- `use burr 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | habit streaks | primary | runs |
| FORTRAN | `trebago` | numeric | progress trend | primary | runs |
| Ada | `eve` | limits | rest limits | primary | runs |
| APL | `kron` | arrays | day grid | primary | runs |
| MUMPS | `myra` | records | goal tree | primary | runs |
| IPL | `-` | lists | lists | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id topiary
matrix 204
name Topiary
needs fs.read
needs fs.write
needs notify
needs ai.ask

provides count_ok truth count cap
logic eve count_ok <<
eve topiary
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — weekly trend exact in FORTRAN as Trebago

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — day grid in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — goal tree in MUMPS as MYRA

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

