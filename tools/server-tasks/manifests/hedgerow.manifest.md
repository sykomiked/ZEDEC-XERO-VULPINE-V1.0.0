# hedgerow — the complete manifest (the parameter document)

**Purpose.** who may reach this machine: network mode (`net.command` off/lan/online), answering peers (`peer.serve`), the phone link (`phone.link`, `phone.list`), switches that expose methods (`features.get`), hosted spaces (`pspace.list`) and the IRC bridge (`irc.status`). Packet filtering and per-port rules have no method: use only existing methods; list what's missing in your report.

**Matrix layer 30 · icon group L1 · wave spec `wave2/030-hedgerow.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id hedgerow
matrix 30
name Hedgerow
needs net
needs peers
needs sys.read
needs notify

provides composite_key num high low
logic kron composite_key <<
0|_high*1000+_low
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "is this path reachable" (mode, switch, private bind) in eve + kron + C++ wasm (three witnesses)

**The clause it implements.** "is this path reachable" (mode, switch, private bind) in eve + kron + C++ wasm (three witnesses)

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

### Entry 2 — address class (loopback, private, link-local, unique-local, from octets) in Rust wasm + kron witnessed

**The clause it implements.** address class (loopback, private, link-local, unique-local, from octets) in Rust wasm + kron witnessed

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

### Entry 3 — Opening anything is asked first

**The clause it implements.** Opening anything is asked first

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

### Entry 4 — closing is at once

**The clause it implements.** closing is at once

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

