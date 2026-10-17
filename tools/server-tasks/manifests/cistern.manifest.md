# cistern — the complete manifest (the parameter document)

**Purpose.** runs a water utility: pipes, pumps, leaks, meter readings, quality tests and customer bills (`files.read`, `files.write`, `journal.query`, `ledger.entries`, `ledger.post`, `print.submit`). zxvd has no method for meter, pump and quality sensor readings: use only existing methods; list what's missing in your report.

**Matrix layer 296 · icon group L3 · wave spec `wave25/296-cistern.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cistern
matrix 296
name Cistern
needs fs.read
needs fs.write
needs journal
needs ledger.read
needs ledger.post
needs print
needs notify

provides grade_band num value b1 b2 b3 b4
provides difference num a b
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
logic kron difference <<
0|_a-_b
.
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `hydraulic` (flow and head loss per pipe from the integer tables) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** `hydraulic` (flow and head loss per pipe from the integer tables) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 2 — `meters` arrays in APL as KRON

**The clause it implements.** `meters` arrays in APL as KRON

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

