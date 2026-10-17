# relume — the complete manifest (the parameter document)

**Purpose.** brings a business or household back after a disaster: restores files, books and services in the right order from signed backups (`backup.list`, `backup.verify`, `backup.restore`), checking the books (`ledger.check`) and the journal (`journal.verify`).

**Matrix layer 273 · icon group L3 · wave spec `wave23/273-relume.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id relume
matrix 273
name Relume
needs backup
needs fs.read
needs fs.write
needs journal
needs ledger.read
needs sys.read
needs notify

provides exact_total num n a b c d
provides ratio_x1000 num num den
provides composite_key num high low
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron ratio_x1000 <<
0|_num*1000%_den
.
logic kron composite_key <<
0|_high*1000+_low
.
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — `restore_next` (the next item to restore: every item it depends on already restored and verified) in Ada as Eve + APL as KRON witnessed

**The clause it implements.** `restore_next` (the next item to restore: every item it depends on already restored and verified) in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — `restored_books` (each restored ledger's balance equals the backup's, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed

**The clause it implements.** `restored_books` (each restored ledger's balance equals the backup's, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed

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

### Entry 3 — `service_deps` (service dependency matrix) in APL as KRON

**The clause it implements.** `service_deps` (service dependency matrix) in APL as KRON

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic trebago <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

