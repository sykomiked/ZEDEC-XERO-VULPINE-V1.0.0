# bitting — the complete manifest (the parameter document)

**Purpose.** issues and revokes digital keys for doors, rooms, vehicles and shared equipment, with a full access history (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `journal.query`). zxvd has no method for door and vehicle lock control: use only existing methods; list what's missing in your report.

**Matrix layer 307 · icon group L3 · wave spec `wave25/307-bitting.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id bitting
matrix 307
name Bitting
needs sys.read
needs peers
needs journal
needs notify
needs ledger.read

provides composite_key num high low
provides flags_none truth bits forbidden
logic kron composite_key <<
0|_high*1000+_low
.
logic kron flags_none <<
(_bits&_forbidden)=0
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `access_pattern` (uses per hour against the holder's usual pattern, integer score) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** `access_pattern` (uses per hour against the holder's usual pattern, integer score) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 2 — `holders` tree in MUMPS as MYRA

**The clause it implements.** `holders` tree in MUMPS as MYRA

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

