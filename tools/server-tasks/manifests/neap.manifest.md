# neap — the complete manifest (the parameter document)

**Purpose.** Keeps double-entry books, reconciles accounts and prepares tax reports exactly: accounts and entries (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.check`), suspense holds (`ledger.hold`, `ledger.holds`, `ledger.release`), statements (`iso20022.render` camt.053), the jurisdiction (`jurisdiction.get`), reports saved and printed (`files.write`, `print.submit`).

**Matrix layer 234 · icon group L2 · wave spec `wave19/234-neap.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id neap
matrix 234
name Neap
needs ledger.read
needs ledger.post
needs fs.write
needs print
needs notify

provides count_ok truth count cap
provides flags_none truth bits forbidden
provides running_state truth state event
provides product num a b
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron flags_none <<
(_bits&_forbidden)=0
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic kron product <<
0|_a*_b
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - (critical) A = L + E after every post in COBOL as Sutra + FORTRAN as Trebago witnessed

**The clause it implements.** - (critical) A = L + E after every post in COBOL as Sutra + FORTRAN as Trebago witnessed

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

### Entry 2 — - depreciation (straight line and declining balance in minor units, the remainder in the last period) in FORTRAN as Trebago + COBOL as Sutra witnessed

**The clause it implements.** - depreciation (straight line and declining balance in minor units, the remainder in the last period) in FORTRAN as Trebago + COBOL as Sutra witnessed

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

### Entry 3 — - general ledger lines refusing on overflow in COBOL as Sutra

**The clause it implements.** - general ledger lines refusing on overflow in COBOL as Sutra

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

### Entry 4 — never re-derive the tables

**The clause it implements.** never re-derive the tables

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

