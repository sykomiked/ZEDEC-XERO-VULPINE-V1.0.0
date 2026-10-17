# trestle — the complete manifest (the parameter document)

**Purpose.** lay out native screens and save them as screen files in the Hoard (`files.read`, `files.write`); simple mode from `ui.get`. zxvd has no toolkit layout method: use only existing methods; list what's missing in your report.

**Matrix layer 131 · icon group L1 · wave spec `wave11/131-trestle.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id trestle
matrix 131
name Trestle
needs fs.read
needs fs.write

provides share_x100 num part whole
provides limit_excess num value limit
provides count_ok truth count cap
provides composite_key num high low
provides difference num a b
logic kron share_x100 <<
0|_part*10000%_whole
.
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
logic kron composite_key <<
0|_high*1000+_low
.
logic kron difference <<
0|_a-_b
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — COBOL owns no decision here

**The clause it implements.** COBOL owns no decision here

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

