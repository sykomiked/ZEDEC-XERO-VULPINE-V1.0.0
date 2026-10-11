# Treaties: COBOL copybooks and Sutra

The first language pillar (COBOL → Sutra). A **treaty** is one batch of work
for the engine (`src/engine/zxv_engine.h`): who takes part, what each party
deposits and may claim back, how the batch's dividend is shared, and who owes
whom. It has one abstract form and two concrete spellings of it:

| Form | Who writes it | Where it is defined |
|------|---------------|---------------------|
| Record | COBOL programs, `CALL 'ZXV_ENGINE' USING ZXV-TREATY ZXV-RESULT` | `copybooks/ZXVTRTY.cpy` (2542 bytes), `copybooks/ZXVRSLT.cpy` (2422 bytes) |
| Sutra | agents and people | the grammar below |

Both parse to the same `zxt_treaty_t`, so either lowers to the other.

```
TREATY CORRIDOR-7 USD SETL
PARTY ACME 5000000 4000 1/3
PARTY BETA 3000000 3000 1/7
PARTY GAMMA 900000 0 11/21
OWE ACME BETA 1200000
OWE BETA GAMMA 700000
OWE GAMMA ACME 100000
END
```

## Layers

| File | Side | Does |
|------|------|------|
| `zxt.h`, `zxt.c` | libzxv (freestanding: no libc, no allocation, no recursion, no floats) | parse, validate and emit both forms; PIC S9(18) arithmetic |
| `zxt_exec.h`, `zxt_exec.c` | userland | run a treaty on the engine; every result is a signed integer inside PIC S9(18) |
| `zxt_cobol.c` | userland | the `ZXV_ENGINE` C entry COBOL calls |

The parser never runs anything. `zxt.c` is built into `libzxv-legacy` and
`libzxv-econ`; the runner and the COBOL entry into `libzxv-econ`
(`packages/Makefile`). Everything here is additive: no existing function,
type or constant changed.

## Sutra grammar

One spelling per treaty. Single spaces, `\n` line ends, nothing after `END\n`.

```
treaty  = "TREATY " name " " asset " " op "\n" party{1,16} owe{0,64} "END\n"
op      = "NET" | "SETL"
party   = "PARTY " id " " deposit " " claim " " num "/" den "\n"
owe     = "OWE " id " " id " " amount "\n"
name    = letter { letter | digit | "-" }   ; 1..30 chars, not ending in "-"
id      = same as name, 1..8 chars, distinct within the treaty
asset   = 3 capital letters
deposit, claim, amount = "0" | nonzero-digit {digit}  ; at most 18 digits
num, den               = same, at most 9 digits
```

Rules checked on every parse and every emit (fail closed, with the byte
offset of the first fault):

* **R1** weights: `1 <= den`, `num <= den`, lowest terms, and they add up to
  exactly 1 (exact integer arithmetic; a sum whose exact value would leave 64
  bits is refused rather than rounded).
* **R2** `claim <= deposit` for every party.
* **R3** every amount fits PIC S9(18); obligations are above zero.
* **R4** 1–16 parties, 0–64 obligations between two different known parties.

In the record, unused slots must be exactly what `INITIALIZE ZXV-TREATY`
leaves (spaces and `+000…`), negative zero is refused, and every numeric
field is DISPLAY text (`SIGN LEADING SEPARATE`), so the record is plain ASCII
on every machine.

## What a run returns

`NET` reports each party's gross net position and the netted legs.
`SETL` also posts the legs with the 0.08889% assurance fee (all or nothing),
pays each claim back out, and reports each party's weight share of what the
fee put in the V-Bill dividend bucket (floor shares, then leftover units to
the largest remainders, ties to the lowest index). That share is reported,
not paid: it stays in the bucket. A refusal anywhere gives a zeroed result
that carries only the status.

## Tests

`make -C kernel test-treaty` and `make -C kernel test-treaty-cobol`
(both part of `verify-all`):

* `test_zxt.c`: every rule, every offset, both round trips, PIC edges.
* `test_zxt_exec.c`: netting, settlement, fees, dividend shares against a
  128-bit reference, conservation, fail-closed paths, the result record.
* `fuzz_zxt.c`: 1,000,000 grammar fuzz cases (generated treaties, mutated
  text and records, raw bytes); every accepted input must re-emit byte for
  byte and satisfy the rules checked a second way.
* `cobol/PICPAR.cob` + `pic_parity.c`: 1,000,000 ADD/SUBTRACT/MULTIPLY/DIVIDE
  vectors on PIC S9(18) with ON SIZE ERROR, GnuCOBOL against `zxt_pic_*`.
* `cobol/TRTYDEMO.cob` + `treaty_parity.c`: a COBOL program fills the
  copybook and calls `ZXV_ENGINE`; its treaty and result records must equal
  the same treaty written in Sutra and run from C, byte for byte.
* `mut_check.sh`: what the mutation harness runs per mutant
  (`tests/harness/mutator/targets.json`, targets `zxt` and `zxt_exec`).

## Honest limits

A treaty is a batch description, not a contract in law: signatures, consent
and authority live elsewhere (pqsec, lpres). One asset per treaty. COBOL
interop is tested with GnuCOBOL 3.1 only; an IBM Enterprise COBOL caller
needs its own C linkage stub and is untested. The older `src/sutra/` module is
a different, program-shaped language over the Vino ledger; this compact
Sutra does not replace or depend on it.
