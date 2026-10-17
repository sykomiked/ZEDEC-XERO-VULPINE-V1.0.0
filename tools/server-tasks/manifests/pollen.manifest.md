# pollen — the complete manifest (the parameter document)

**Purpose.** offer and fetch on the local network (`net.command` lan, share, want; `net.offers`, `net.peers`), chunk with `freight.pack` and rebuild with `freight.unpack`, read and save in the Hoard (`files.read`, `files.write`), out of a space only through `files.cross`.

**Matrix layer 31 · icon group L1 · wave spec `wave5/031-pollen.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id pollen
matrix 31
name Pollen
needs peers
needs fs.read
needs fs.write
needs notify

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

### Entry 1 — "fits on the receiver" in sutra + kron witnessed

**The clause it implements.** "fits on the receiver" in sutra + kron witnessed

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

### Entry 2 — chunk plan (freights and packets for a size and k) exact in trebago + kron witnessed

**The clause it implements.** chunk plan (freights and packets for a size and k) exact in trebago + kron witnessed

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

### Entry 3 — tests stay on lan or loopback

**The clause it implements.** tests stay on lan or loopback

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

