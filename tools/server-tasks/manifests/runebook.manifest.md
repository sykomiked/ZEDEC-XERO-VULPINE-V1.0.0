# runebook — the complete manifest (the parameter document)

**Purpose.** every character, emoji and symbol by script and block, with favourites saved in the Hoard (`files.read`, `files.write`); language names from `xlate.languages`. zxvd has no character-data method: use only existing methods; list what's missing in your report.

**Matrix layer 114 · icon group L1 · wave spec `wave10/114-runebook.md`.**

## The manifest: the parts the pattern library already makes correct

Every line below is FINAL — the provides, the body and the check are the
library's verified implementations. Emit them exactly.

```zxapp
zxapp 1
id runebook
matrix 114
name Runebook
needs fs.read
needs fs.write

provides count_ok truth count cap
logic eve count_ok <<
eve app
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
}
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## The app-specific entries — SPELLED OUT TO THE LETTER

These are the entries the library does not cover. Each one below
gives the exact `provides` line, the runtime, the body's required
shape (from the real blocks) and the exact check. Write the body;
nothing else is yours to decide.

### Entry 1 — valid code point (0..0x10FFFF, not a surrogate, not a noncharacter) in kron + trebago witnessed

**The clause it implements.** valid code point (0..0x10FFFF, not a surrogate, not a noncharacter) in kron + trebago witnessed

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

### Entry 2 — UTF-8 length and bytes of a code point in trebago + kron witnessed

**The clause it implements.** UTF-8 length and bytes of a code point in trebago + kron witnessed

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

