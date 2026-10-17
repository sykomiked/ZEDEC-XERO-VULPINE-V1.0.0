# seedbank — the complete manifest (the parameter document)

**Purpose.** pin and publish by CID (`aipi.put`, `aipi.status`, `web4.cid`), fetch (`aipi.get`), record a published space's CID (`spaces.set_cid`). Unpinning and announcing to the network have no method: use only existing methods; list what's missing in your report.

**Matrix layer 33 · icon group L1 · wave spec `wave1/033-seedbank.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id seedbank
matrix 33
name Seedbank
needs net
needs fs.read
needs fs.write

provides limit_excess num value limit
provides share_x100 num part whole
provides composite_key num high low
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron share_x100 <<
0|_part*10000%_whole
.
logic kron composite_key <<
0|_high*1000+_low
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — CID string check in Rust wasm + kron witnessed (the same rule as Ramble's

**The clause it implements.** CID string check in Rust wasm + kron witnessed (the same rule as Ramble's

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

### Entry 2 — copy it as a block)

**The clause it implements.** copy it as a block)

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

