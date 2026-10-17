# fusee — the complete manifest (the parameter document)

**Purpose.** Fronts the boot chain: the UEFI stubs (kernel/arch/x86_64/efi_stub.c, kernel/arch/arm64/efi_stub.c, kernel/arch/efi_models.h), measured boot (kernel/src/bootattest), boot features (kernel/src/bootfeat), A/B slots (kernel/src/loader/abupdate.c), update checks (kernel/src/update, zx_upcheck.c) and hardware keys (kernel/src/hwkey). Signed update check (`update.check`), the journal's chain (`journal.verify`, `journal.query`), switches (`features.get`), the machine (`sys.info`). Secure Boot keys, boot entries, A/B slot selection, firmware capsules, microcode and reading the measured-boot log have no method: use only existing methods; list what's missing in your report.

**Matrix layer 247 · icon group L2 · wave spec `wave20/247-fusee.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id fusee
matrix 247
name Fusee
needs sys.read
needs journal
needs net
needs notify

provides difference num a b
provides composite_key num high low
provides count_ok truth count cap
provides running_state truth state event
logic kron difference <<
0|_a-_b
.
logic kron composite_key <<
0|_high*1000+_low
.
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — - (critical) "may boot this image" = both signature checks pass AND the stub is signed AND the version is at or above the rollback floor, in Ada as Eve + APL as KRON witnessed

**The clause it implements.** - (critical) "may boot this image" = both signature checks pass AND the stub is signed AND the version is at or above the rollback floor, in Ada as Eve + APL as KRON witnessed

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

### Entry 2 — - firmware version arithmetic (major.minor.patch packed into one integer, distance to the floor) exact in FORTRAN as Trebago

**The clause it implements.** - firmware version arithmetic (major.minor.patch packed into one integer, distance to the floor) exact in FORTRAN as Trebago

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

### Entry 3 — - firmware inventory tree in MUMPS as MYRA

**The clause it implements.** - firmware inventory tree in MUMPS as MYRA

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

### Entry 4 — - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

**The clause it implements.** - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

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

### Entry 5 — Paraconsistent (BOTH test, in test_zxapp.c): one signature check passes and the other fails on the same image, giving BOTH

**The clause it implements.** Paraconsistent (BOTH test, in test_zxapp.c): one signature check passes and the other fails on the same image, giving BOTH

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**The provides line (copy exactly, choose the entry name):**

```
provides <entry_name> num|truth <arg> <arg> ...
```

**The body (this shape, this runtime):**

```
logic kron <entry_name> <<
<the source>
.
```

**The check (one line; the inputs are integers, the answers the
rows):**

```
check <entry_name> <arg>=<int>,... = <answer>,...
```

### Entry 6 — never re-derive the tables

**The clause it implements.** never re-derive the tables

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

