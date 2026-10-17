# fireglow — the complete manifest (the parameter document)

**Purpose.** episodes and books in the Hoard (`files.list`, `files.read`), positions saved (`files.write`). Fetching feeds has no method: use only existing methods; list what's missing in your report.

**Matrix layer 52 · icon group L1 · wave spec `wave7/052-fireglow.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id fireglow
matrix 52
name Fireglow
needs fs.read
needs fs.write
needs net

provides difference num a b
provides ratio_x1000 num num den
provides share_x100 num part whole
provides limit_excess num value limit
provides composite_key num high low
provides running_state truth state event
logic kron difference <<
0|_a-_b
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
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
logic kron composite_key <<
0|_high*1000+_low
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — chapter from position in kron

**The clause it implements.** chapter from position in kron

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

