# volute — the complete manifest (the parameter document)

**Purpose.** runs a fish or shellfish farm: water readings, feeding schedules, stock health and harvest sales (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `ledger.hold`, `place.here`). zxvd has no method for water probe readings: use only existing methods; list what's missing in your report.

**Matrix layer 280 · icon group L3 · wave spec `wave23/280-volute.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id volute
matrix 280
name Volute
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs location
needs notify

provides count_ok truth count cap
provides product num a b
provides composite_key num high low
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron product <<
0|_a*_b
.
logic kron composite_key <<
0|_high*1000+_low
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `oxygen` (dissolved oxygen trend and growth per day, integer model) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** `oxygen` (dissolved oxygen trend and growth per day, integer model) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 2 — `pens` sensor arrays in APL as KRON

**The clause it implements.** `pens` sensor arrays in APL as KRON

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

