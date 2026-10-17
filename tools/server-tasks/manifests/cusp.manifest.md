# cusp — the complete manifest (the parameter document)

**Purpose.** manages livestock: herd records, breeding, grazing rotation, vet visits and sales (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`, `place.list`).

**Matrix layer 287 · icon group L3 · wave spec `wave24/287-cusp.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cusp
matrix 287
name Cusp
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs location
needs net
needs notify

provides modular_draw num seed members step
provides product num a b
provides difference num a b
logic kron modular_draw <<
0|(_seed*1103515245+12345+_step*7)%_members
.
logic kron product <<
0|_a*_b
.
logic kron difference <<
0|_a-_b
.
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `paddocks` grid in APL as KRON

**The clause it implements.** `paddocks` grid in APL as KRON

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

### Entry 2 — `pedigree` tree in MUMPS as MYRA

**The clause it implements.** `pedigree` tree in MUMPS as MYRA

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

