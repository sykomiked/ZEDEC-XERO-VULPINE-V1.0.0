# piccolo — the complete manifest (the parameter document)

**Purpose.** Plans pollinator-friendly gardens and tracks bees, birds and blooms with neighbours: beds and sightings in the Hoard (`files.read`, `files.write`, `search.query`), places (`place.here`, `place.list`), the neighbours' circle (`social.post`, `social.feed`).

**Matrix layer 385 · icon group L3 · wave spec `wave32/385-piccolo.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id piccolo
matrix 385
name Piccolo
needs fs.read
needs fs.write
needs search
needs location
needs peers
needs net

provides limit_excess num value limit
provides composite_key num high low
provides ratio_x1000 num num den
provides modular_draw num seed members step
provides running_state truth state event
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron composite_key <<
0|_high*1000+_low
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - `bloom`: (critical) bloom timing (degree days to flowering by species table) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** - `bloom`: (critical) bloom timing (degree days to flowering by species table) in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic c <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

### Entry 2 — - `planting`: planting layout across beds (bloom gaps filled month by month) in APL as KRON

**The clause it implements.** - `planting`: planting layout across beds (bloom gaps filled month by month) in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic sutra <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

### Entry 3 — - `calendar`: bloom calendar grid in APL as KRON

**The clause it implements.** - `calendar`: bloom calendar grid in APL as KRON

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic trebago <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

### Entry 4 — - `species`: species tree in MUMPS as MYRA

**The clause it implements.** - `species`: species tree in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic eve <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

### Entry 5 — Paraconsistent (BOTH test, in test_zxapp.c): two neighbours report a rare bee and a common one for the same photo: the sighting is BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): two neighbours report a rare bee and a common one for the same photo: the sighting is BOTH

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic kron <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

### Entry 6 — never re-derive the tables

**The clause it implements.** never re-derive the tables

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic myra <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

