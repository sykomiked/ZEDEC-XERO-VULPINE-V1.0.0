# cartulary — the complete manifest (the parameter document)

**Purpose.** indexed, versioned records in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`), found with `search.query`, each version kept with `backup.versions` and `backup.list`, and proven years later against the journal (`journal.query`, `journal.verify`). zxvd has no method for a retention-hold flag in the daemon (kept as a Hoard file instead): use only existing methods; list what's missing in your report.

**Matrix layer 145 · icon group L2 · wave spec `wave12/145-cartulary.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id cartulary
matrix 145
name Cartulary
needs fs.read
needs fs.write
needs search
needs backup
needs journal

provides composite_key num high low
provides limit_excess num value limit
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
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "may this version be pruned" = schedule over AND no hold AND `keepsake.keep_ok` in Ada as Eve + APL as KRON witnessed

**The clause it implements.** "may this version be pruned" = schedule over AND no hold AND `keepsake.keep_ok` in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — storage growth (bytes per month as a vector, projected bytes after n months from the mean monthly growth, floor) exact in FORTRAN as Trebago

**The clause it implements.** storage growth (bytes per month as a vector, projected bytes after n months from the mean monthly growth, floor) exact in FORTRAN as Trebago

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

