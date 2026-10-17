# stoker — the complete manifest (the parameter document)

**Purpose.** runs models on the fastest hardware present, GPU first: picks the backend, places weights in video memory, checks each model's signature and licence (`model.info`), and reports speed per watt (`power.status`, `power.top`, `sys.info`, `swarm.state`). zxvd has no method for GPU and backend enumeration, video-memory placement: use only existing methods; list what's missing in your report.

**Matrix layer 196 · icon group L2 · wave spec `wave16/196-stoker.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id stoker
matrix 196
name Stoker
needs ai.ask
needs sys.read
needs swarm
needs fs.read

provides composite_key num high low
logic kron composite_key <<
0|_high*1000+_low
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — "admit this model" (signed AND licence allows commercial use) in Ada as Eve + APL as KRON witnessed, refusing unsigned or non-commercial weights

**The clause it implements.** "admit this model" (signed AND licence allows commercial use) in Ada as Eve + APL as KRON witnessed, refusing unsigned or non-commercial weights

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

### Entry 2 — tokens per second per watt (tokens x 10^6 / microjoules) in FORTRAN as Trebago + APL as KRON witnessed

**The clause it implements.** tokens per second per watt (tokens x 10^6 / microjoules) in FORTRAN as Trebago + APL as KRON witnessed

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

### Entry 3 — layers placed in video memory (largest prefix of layer bytes that fits the free bytes) in APL as KRON + FORTRAN as Trebago witnessed

**The clause it implements.** layers placed in video memory (largest prefix of layer bytes that fits the free bytes) in APL as KRON + FORTRAN as Trebago witnessed

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

### Entry 4 — model catalogue in MUMPS as MYRA

**The clause it implements.** model catalogue in MUMPS as MYRA

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

