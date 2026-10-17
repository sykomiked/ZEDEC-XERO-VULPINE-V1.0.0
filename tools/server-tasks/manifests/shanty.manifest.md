# shanty — the complete manifest (the parameter document)

**Purpose.** Forecasts tides, waves and coastal flooding for harbours and shore towns: districts (`place.here`, `place.list`), stations (`net.peers`, `devmesh.devices`), records (`files.read`, `files.write`), alerts sent once a person approves (`pspace.post`), history (`journal.query`). Gauge and buoy readings have no method: use only existing methods; list what's missing in your report.

**Matrix layer 362 · icon group L3 · wave spec `wave30/362-shanty.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id shanty
matrix 362
name Shanty
needs location
needs peers
needs net
needs fs.read
needs fs.write
needs journal
needs notify

provides grade_band num value b1 b2 b3 b4
provides ratio_x1000 num num den
provides within_range truth value lo hi
provides count_ok truth count cap
provides running_state truth state event
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic eve within_range <<
eve app
values = 0 .. 100

within_range {
  value: values 0 .. 100
}
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - `send_alert`: alert routing (district to channel) in APL as KRON

**The clause it implements.** - `send_alert`: alert routing (district to channel) in APL as KRON

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

### Entry 2 — - `tides`: tide grids in APL as KRON

**The clause it implements.** - `tides`: tide grids in APL as KRON

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

### Entry 3 — - `dues`: harbour dues (minor units, refusing on overflow) in COBOL as Sutra

**The clause it implements.** - `dues`: harbour dues (minor units, refusing on overflow) in COBOL as Sutra

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

### Entry 4 — - `coast`: coastline tree in MUMPS as MYRA

**The clause it implements.** - `coast`: coastline tree in MUMPS as MYRA

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

### Entry 5 — - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

**The clause it implements.** - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

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

### Entry 6 — Paraconsistent (BOTH test, in test_zxapp.c): two forecast models give 2.1 m and 2.9 m for the same high water: the forecast is BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): two forecast models give 2.1 m and 2.9 m for the same high water: the forecast is BOTH

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

### Entry 7 — never re-derive the tables

**The clause it implements.** never re-derive the tables

**Runtime.** `-` — see the catalogue

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic - <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

