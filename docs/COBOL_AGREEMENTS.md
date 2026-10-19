# COBOL corridor agreements

COBOL is the proposed declarative agreement language for bilateral corridors.
The existing C implementation in `kernel/src/legacy/cobol.c` parses a bounded
copybook subset and decodes packed decimal, zoned decimal and binary records.
The existing `bindings/cobol/payvalidate.cob` calls the C codec ABI. Neither is
an arbitrary COBOL procedure compiler or a corridor settlement interpreter.

`bindings/cobol/corridor.cpy` defines the proposed versioned agreement record.
Amounts are scaled integers with their currency and minor-unit scale supplied
by the surrounding settlement instruction. The fee uses separate integer
numerator and denominator fields, set to 8 and 9000. Compression is basket
valuation metadata; the collateral limit is a ceiling, not an allocation or
evidence of custody. Agreement acceptance must verify its version, both
distinct counterparties, exact fee, amount bounds, identities and signatures.

Fixed-point decimal represents terminating decimal fractions exactly, but it
does not represent a repeating rational such as 8/9000 without rounding.
`PIC V9(05) VALUE 0.08889` cannot be used as the authoritative settlement rate.
Cross-currency conversion also needs an explicit funded exchange-rate source.

Executing an agreement must dispatch only specified, bounded operations into
the existing C settlement ledger. Unsupported statements must be rejected;
silently skipping an unknown clause changes the signed agreement's meaning.
The decoded record, its canonical representation, its signed source and the
interpreter version need traceable hashes. Admission, replays, amendments and
revocation need explicit authorization and journal rules.

An imbalance can preserve contradictory evidence in the paraconsistent core.
It cannot authorize settlement through unfunded collateral. Before retrying a
payment, the caller must explicitly allocate already funded collateral and
the ledger must commit balanced postings. Dividend eligibility is any account
that has opted in; an agreement must not add an active-session requirement.

## Validation still required

- A strict versioned agreement parser and bounded operation interpreter,
  integrated with the existing payment ledger and authorization boundary.
- Negative tests for unsupported syntax, duplicate clauses, malformed pictures,
  overflow, truncated records, negative amounts and mismatched signed content.
- Differential record decoding against GnuCOBOL, followed by independent exact
  arithmetic and ledger conservation checks.
- IBM z/OS compiler, calling convention, character encoding and endian tests.
  The currently advertised five architecture boots do not validate IBM z/OS.

Readable syntax does not establish legal enforceability or security. Those
remain separate reviews of the agreement and the implementation.

## Sutra intent bridge

The subsequent architecture proposal makes Sutra the compact agent-intent
layer above these COBOL agreements and the C runtime. Its example mints a
basket with a 1/100 V-Bill anchor, 45/100 NGN, 45/100 KES and 9/100 cocoa;
the complete weights sum exactly to one. The anchor compression is 10000,
the currency factors are 1, and the cocoa factor is 50. These are valuation
parameters, not exchange-rate evidence or custody deposits.

The current `kernel/src/sutra` parser implements the documented
`SUTRA-PROGRAM`/ordinal-step grammar against the existing Vino ledger.
It does not implement the proposed `@CHIP-MINT` and `@DISPATCH` grammar or
a semantics-preserving compiler to canonical COBOL. Those features remain
requirements; a successful existing Sutra test does not validate this syntax.

The bridge must use one versioned, typed intermediate representation shared
by both front ends. It must preserve account and corridor identifiers, exact
rational weights, compression factors, amount units, authorization scope,
collateral references and supported operations. Both front ends must reject
duplicate assets, nonpositive denominators, negative weights, overflow and
weights not summing exactly to one before any ledger mutation.

Round-trip and differential tests must demonstrate equal normalized intents
and equal ledger effects for equivalent COBOL and Sutra inputs. Test the four
logical states by meaning rather than assuming their enum values agree: the
blueprint's FALSE=1/TRUE=2 encoding differs from LPRES's TRUE=1/FALSE=2.
Contradiction must remain explicit and cannot bypass the signed agreement's
funding or authorization constraints. Grammar validity alone does not prevent
an agent from issuing a syntactically valid but unauthorized instruction.

The zero-heap hot-path requirement needs bounded parsing and execution tests
with allocation disabled, explicit size/depth/instruction limits and atomic
failure. Nanosecond parsing and z/OS interoperability need measurements on
their actual targets; they are not established by the supplied narrative.
