# pavise — the complete manifest (the parameter document)

**Purpose.** one security perimeter combining firewall paths, app permissions and keys into a single policy, read from `features.get`, `sys.net`, `net.peers`, `devmesh.devices`, `passkeys.list`, with its history from `journal.query` and `journal.verify`. zxvd has no method for firewall rule write, capability grant write: use only existing methods; list what's missing in your report.

**Matrix layer 153 · icon group L2 · wave spec `wave12/153-pavise.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id pavise
matrix 153
name Pavise
needs sys.read
needs net
needs peers
needs journal
needs notify

provides composite_key num high low
provides limit_excess num value limit
provides flags_none truth bits forbidden
provides clamp num value lo hi
logic kron composite_key <<
0|_high*1000+_low
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron flags_none <<
(_bits&_forbidden)=0
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — trust tree in MUMPS as MYRA

**The clause it implements.** trust tree in MUMPS as MYRA

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

