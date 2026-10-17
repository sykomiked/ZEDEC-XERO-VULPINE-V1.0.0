# lozenge — the exhaustive specification

| field | value |
|---|---|
| app id | `lozenge` |
| matrix layer | 281 |
| icon group | L3 |
| wave spec | `wave23/281-lozenge.md` |
| status | pending |

## 1. Purpose

tracks hives, pollination contracts with growers and honey sales for beekeepers and their co-op (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`).

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs ledger.post`
- `needs fs.read`
- `needs fs.write`
- `needs net`
- `needs peers`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use skep 1` — call `<id>.<entry>` after the use line
- `use sheaf 1` — call `<id>.<entry>` after the use line
- `use neap 1` — call `<id>.<entry>` after the use line
- `use fairground 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | pollination invoices | primary | runs |
| FORTRAN | `trebago` | numeric | colony strength model | primary | runs |
| Ada | `eve` | limits | pesticide distance limits | primary | runs |
| APL | `kron` | arrays | hive grid | primary | runs |
| MUMPS | `myra` | records | colony lineage tree | primary | runs |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id lozenge
matrix 281
name Lozenge
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs net
needs peers

provides ratio_x1000 num num den
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
```

## 6. The entry specifications (what each entry computes)

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `placement_ok` (a hive at least the declared distance from any field sprayed in the last n days) in Ada as Eve + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `colony_strength` (frames of bees and brood, weekly trend) in FORTRAN as Trebago + APL as KRON witnessed

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

### 7.3 — `hives` grid in APL as KRON

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`count.tb`):**

```
trebago kind_count
  in  kind(n), want
  out count
  count = sum(i, max(0, min(1, 1 + kind(i) - want)) * max(0, min(1, 1 + want - kind(i))))
end
```

### 7.4 — `lineage` tree (queen lines) in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`transfer.eve`):**

```
eve coffer
minor_amount = 1 .. 9007199254740991
minor_left = 0 .. 999999999999999999
minor_gap = 0 .. 0

transfer_rule {
  amount: minor_amount
  after: minor_left
  room: minor_left
  gap: minor_gap
}
```

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

