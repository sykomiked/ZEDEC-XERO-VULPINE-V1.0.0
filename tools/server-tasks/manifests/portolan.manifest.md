# portolan — the complete manifest (the parameter document)

**Purpose.** Surveys and publishes maps of land, buildings and routes, merging field notes, drone scans and old charts: places (`place.here`, `place.list`, `place.add`), notes and scans in the Hoard (`files.read`, `files.write`, `search.query`), the published map by CID (`aipi.put`, `web4.cid`, `spaces.set_cid`). Drone flight logs and chart image import have no method: use only existing methods; list what's missing in your report.

**Matrix layer 344 · icon group L3 · wave spec `wave29/344-portolan.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id portolan
matrix 344
name Portolan
needs location
needs fs.read
needs fs.write
needs search
needs net

provides limit_excess num value limit
provides count_ok truth count cap
provides product num a b
provides running_state truth state event
provides clamp num value lo hi
provides modular_draw num seed members step
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron product <<
0|_a*_b
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - `merge_ok`: (critical) "merge this source" (its survey closes within tolerance AND its date is known AND its datum is one of the declared ones) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** - `merge_ok`: (critical) "merge this source" (its survey closes within tolerance AND its date is known AND its datum is one of the declared ones) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — - `project`: (critical) projection from microdegrees to grid millimetres by an integer table (transverse Mercator series in fixed point) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** - `project`: (critical) projection from microdegrees to grid millimetres by an integer table (transverse Mercator series in fixed point) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 3 — - `elevation`: elevation grids (cells x metres, slope per cell) in APL as KRON

**The clause it implements.** - `elevation`: elevation grids (cells x metres, slope per cell) in APL as KRON

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

### Entry 4 — - `places`: place hierarchy (region, parcel, building, route) in MUMPS as MYRA

**The clause it implements.** - `places`: place hierarchy (region, parcel, building, route) in MUMPS as MYRA

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

### Entry 5 — never re-derive the tables

**The clause it implements.** never re-derive the tables

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

