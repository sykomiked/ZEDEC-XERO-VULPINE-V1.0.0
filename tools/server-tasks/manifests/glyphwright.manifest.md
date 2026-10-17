# glyphwright — the complete manifest (the parameter document)

**Purpose.** keyboard layouts and input methods for every script; languages from `xlate.languages`. zxvd has no layout or input-method method: use only existing methods; list what's missing in your report.

**Matrix layer 137 · icon group L1 · wave spec `wave11/137-glyphwright.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id glyphwright
matrix 137
name Glyphwright
needs fs.read

provides composite_key num high low
provides grade_band num value b1 b2 b3 b4
logic kron composite_key <<
0|_high*1000+_low
.
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — Hangul L, V, T by 0xAC00 + (L x 21 + V) x 28 + T) in myra + kron witnessed

**The clause it implements.** Hangul L, V, T by 0xAC00 + (L x 21 + V) x 28 + T) in myra + kron witnessed

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

### Entry 2 — result is a valid code point (0..0x10FFFF, not a surrogate) in eve

**The clause it implements.** result is a valid code point (0..0x10FFFF, not a surrogate) in eve

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

