# insolation — the complete manifest (the parameter document)

**Purpose.** runs a community wind and solar co-op: generation, storage, shares, mutual-aid grants and grid exports (`power.status`, `ledger.entries`, `ledger.post`, `ledger.hold`, `devmesh.devices`). zxvd has no method for turbine, inverter and meter readings: use only existing methods; list what's missing in your report.

**Matrix layer 335 · icon group L3 · wave spec `wave28/335-insolation.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id insolation
matrix 335
name Insolation
needs sys.read
needs ledger.read
needs ledger.post
needs peers
needs notify

provides count_ok truth count cap
provides ratio_x1000 num num den
provides share_x100 num part whole
provides largest_remainder num pool w1 w2 w3 w4
logic eve count_ok <<
eve {app}
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
logic kron largest_remainder <<
0|(_pool*_w1)%(_w1+_w2+_w3+_w4)
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check share_x100 part=0 whole=1 = 0
check share_x100 part=1 whole=1 = 10000
check share_x100 part=1 whole=10000 = 1
check share_x100 part=3 whole=4 = 7500
check share_x100 part=2 whole=3 = 20000/3
check share_x100 part=1 whole=2 = 5000
check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,10/3,7/3,50
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `assets` grid (turbine and panel) in APL as KRON

**The clause it implements.** `assets` grid (turbine and panel) in APL as KRON

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

**Rules that the node enforces for this runtime:** the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

