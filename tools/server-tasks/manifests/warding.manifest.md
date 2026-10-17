# warding — the complete manifest (the parameter document)

**Purpose.** issues and revokes keys for doors, devices and accounts from one place (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `seats.list`). zxvd has no method for door lock control (as Homestead reports): use only existing methods; list what's missing in your report.

**Matrix layer 173 · icon group L2 · wave spec `wave14/173-warding.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id warding
matrix 173
name Warding
needs sys.read
needs peers
needs notify

provides composite_key num high low
provides limit_excess num value limit
provides flags_none truth bits forbidden
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
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — access grid (holder by door or device) in APL as KRON

**The clause it implements.** access grid (holder by door or device) in APL as KRON

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

### Entry 2 — holder tree in MUMPS as MYRA

**The clause it implements.** holder tree in MUMPS as MYRA

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

