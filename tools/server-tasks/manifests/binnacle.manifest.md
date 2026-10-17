# binnacle — the complete manifest (the parameter document)

**Purpose.** position, heading and routes worked out offline from star sights, the clock (`sys.info`) and saved places (`place.here`, `place.list`, `place.add`), with no outside service. zxvd has no method for star-sight entry from an instrument, chart data: use only existing methods; list what's missing in your report.

**Matrix layer 147 · icon group L2 · wave spec `wave12/147-binnacle.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id binnacle
matrix 147
name Binnacle
needs location
needs sys.read
needs notify

provides ratio_x1000 num num den
provides limit_excess num value limit
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — bearing and distance to every waypoint (fixed point, over the waypoint vectors) in APL as KRON

**The clause it implements.** bearing and distance to every waypoint (fixed point, over the waypoint vectors) in APL as KRON

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

### Entry 2 — distance run per leg in metres, refused on overflow, in COBOL as Sutra + FORTRAN as Trebago witnessed

**The clause it implements.** distance run per leg in metres, refused on overflow, in COBOL as Sutra + FORTRAN as Trebago witnessed

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

### Entry 3 — waypoint tree in MUMPS as MYRA

**The clause it implements.** waypoint tree in MUMPS as MYRA

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

