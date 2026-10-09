<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZEDEC Legacy Bridge — Operator Guide

This is the plain-language guide for a bank or central-bank operator wiring
legacy infrastructure into the ZEDEC platform. It explains what each bridge
part does, how to connect a COBOL batch job or a USSD gateway, what is **not**
included, and the security rules that are not optional.

The bridge lives in two places:

- `kernel/src/legacy/` — the freestanding C modules (telephony signalling,
  DTMF, EBCDIC, COBOL, Fortran).
- `bindings/` — the stable C ABI (`zx_legacy_api.h` → `libzxlegacy`) and the
  language bindings that call it.

Everything in `kernel/src/legacy/` is freestanding integer C: no libc, no
floating point, no dynamic memory, bounded parsers that never read or write
past a buffer. Amounts are exact integers end to end.

---

## 1. What each part does

### Telephony signalling (`sip.*`, `sdp.*`, `ss7.*`, `tcap.*`, `gsm_encode.*`, `isup_map.*`)

- **SIP (RFC 3261)** — parses and builds SIP requests and responses, including
  the compact header forms, with the body returned as a byte span. **SDP
  (RFC 4566)** parses and builds audio offers/answers (payload types, rtpmap,
  direction). This is message syntax only: no transaction/dialog state machine,
  no DNS, no Digest auth, no TLS, no transport.
- **SS7 over SIGTRAN** — a data model, not a network stack: **M3UA (RFC 4666)**
  DATA messages, **SCCP UDT (ITU Q.713)** with ITU **global title**, **TCAP
  (Q.773)** Begin/Continue/End over BER, and enough **MAP (3GPP TS 29.002)** to
  carry **USSD** (`processUnstructuredSS-Request` / `unstructuredSS-Request`)
  and an **SMS MO/MT forward**. **GSM 7-bit packing and UCS-2** (TS 23.038)
  encode the USSD/SMS text. A worked path assembles a full USSD request down
  through MAP → TCAP → SCCP → M3UA and parses it back up.
- **ISUP ↔ SIP mapping** — a table plus functions mapping ISUP IAM/ACM/ANM/
  REL/RLC (Q.764) to SIP INVITE/180/200/BYE (RFC 3398) and Q.850 causes to SIP
  status codes, both directions.

### DTMF (`dtmf.*`)

- An integer **Goertzel** detector for the 16 tones on 8 kHz 16-bit PCM, with
  twist and dominance checks in the spirit of ITU-T Q.24.
- A table-based integer **tone generator** (no floating point).
- **RFC 4733** telephone-event RTP payload encode/decode.
- **SIP INFO** `application/dtmf-relay` body parse/build.
- A small **menu driver** so a phone caller can navigate the assistant or a
  payment menu. A "confirm" transition sets `needs_strong_auth` — see §4.

### Mainframe data and COBOL (`ebcdic.*`, `cobol.*`)

- **EBCDIC ↔ UTF-8** for CCSID 037, 500 and 1047 (full round-trip tables).
- **COBOL data types**: COMP-3 packed decimal, zoned decimal (signed
  overpunch), COMP/COMP-4/COMP-5 big-endian binary, PIC X/9 with implied
  decimal `V`. A **copybook parser** (levels 01-49, PIC, USAGE, fixed OCCURS,
  REDEFINES) produces a field layout; a **record codec** reads and writes
  fields by name. **Fixed-length** and **IBM RDW variable-length** record
  readers iterate a file image.

### Fortran (`fortran.*`)

- **Unformatted sequential record markers** — 4-byte and 8-byte, both byte
  orders, with trailing-marker validation.
- **IBM hexadecimal floating point ↔ IEEE 754** at the bit-pattern level, in
  integer arithmetic only (no float types), for binary32 and binary64.

---

## 2. Wiring a COBOL batch job

1. Build the bridge library: `cd bindings && ./build_lib.sh`. This produces
   `libzxlegacy.a` and `libzxlegacy.so`.
2. Describe your record once as a copybook (see `bindings/cobol/txnrec.cpy`).
   The same copybook shapes feed the C copybook parser if you process records
   in C, or your COBOL `COPY` statement if you process them in COBOL.
3. In your COBOL program, `CALL` the ABI entry points to convert values. The
   worked example `bindings/cobol/payvalidate.cob` decodes a `COMP-3` amount to
   an exact integer and re-encodes it, proving the packed-decimal path.
4. Pass packed/alpha fields `BY REFERENCE` and counts `BY VALUE`. Make the
   library visible to GnuCOBOL's run-time `CALL` resolver (the example uses
   `LD_PRELOAD`; on z/OS, bind the DLL).

A batch pipeline typically: reads the dataset with the fixed-length or RDW
reader → decodes each field with the COBOL codec → hands exact integer amounts
to the platform's payment rails (DEBIT 555 / CREDIT 777 / EQUITY 888) → writes
results back in the same record format.

## 3. Wiring a USSD gateway (mobile money)

The bridge builds and parses the MAP/TCAP/SCCP/M3UA message structure a USSD
menu needs; it does **not** connect to an SS7 network (see §5). Your SIGTRAN
stack or SS7 gateway provides the actual association and routing.

1. For an incoming USSD request, hand the SCCP user data to `tcap_parse`, then
   `map_parse_invoke`, then `map_parse_ussd_arg`, then `gsm7_unpack` to recover
   the dialled string (e.g. `*182*1*500#`). The one-call helper
   `zx_ussd_parse_request` does the MAP+GSM steps together.
2. Drive the `dtmf`/menu or your own USSD menu logic to decide the response.
3. Build the reply with `zx_ussd_build_request` (or the individual builders)
   and hand the bytes back to your SIGTRAN stack to send.

## 4. Security: DTMF and USSD are not authentication

- **A DTMF digit confirming a payment is a UI gesture, nothing more.** The menu
  driver flags a confirm transition by setting `needs_strong_auth`; the host
  **must** run the platform's configured strong authentication before moving
  any money. Tones can be injected, replayed, or carried in-band by anyone on
  the audio path.
- **USSD is transport, not identity.** Treat the MSISDN as a claim to be
  verified, not proof of the account holder.

## 5. What is NOT included (honest limits)

This bridge is data structures, codecs and message framing. It deliberately
does **not** provide, and the platform must obtain separately:

- **Carrier interconnect.** No SCTP associations, no M3UA ASP state machine
  (ASPUP/ASPAC), no MTP3 routing, no point-code allocation. There is no
  connection to any PSTN, mobile core, or signalling network.
- **SS7 licensing and global-title screening.** Operating on the SS7/SIGTRAN
  network requires agreements with carriers, allocated point codes and GT
  ranges, and inbound GT screening — none of which is code.
- **Certification and conformance.** Nothing here is a certified Q.24 DTMF
  receiver, a certified SIP stack, or a MAP/CAMEL conformance-tested node.
  Passing our tests is not carrier acceptance testing.
- **Carrier-grade media.** The DTMF front-end has no echo control, AGC, or
  Type-1/Type-2 receiver suite; the SIP layer runs no transactions or TLS.
- **Full COBOL/Fortran semantics.** The copybook parser is a pragmatic subset
  (no edited pictures, COMP-1/2 float, nested or DEPENDING-ON OCCURS, or
  88-levels); the FP converter is a bit-pattern tool, not a numerics library.

## 6. Security note: SS7 is unauthenticated — firewall it

SS7 (and its SIGTRAN carriage) has **no built-in authentication**. On a real
network, a peer that can reach your signalling endpoint can attempt location
tracking, call/SMS interception, and USSD fraud. Treat every SS7/SIGTRAN link
as hostile:

- Terminate SIGTRAN only on screened, firewalled interfaces; never expose the
  signalling plane to the public internet.
- Apply inbound and outbound **global-title screening** and category controls
  at the STP/gateway; drop MAP operations you do not expect from peers that
  should not send them.
- Rate-limit and log USSD and SMS-forward operations; alert on anomalies.
- Keep the signalling plane isolated from the application and payment planes;
  the bridge hands you parsed structures, but you decide what to trust.

The bridge cannot enforce any of this — it has no network. These are operator
responsibilities on the infrastructure around it.

---

## 7. Tests

Each module has a host test built with
`gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST`. Current counts:

| Suite | Checks |
|-------|--------|
| SIP/SDP (incl. fuzz) | 50 |
| SS7/TCAP/MAP/GSM (incl. fuzz) | 46 |
| ISUP↔SIP | 33 |
| DTMF | 61 |
| EBCDIC | 795 |
| COBOL | 49 |
| Fortran/HFP | 36 |
| C ABI | 14 |

Published test vectors are used where they exist: the RFC 3261/4566 example
messages, the GSM 03.38 `"hellohello"` packed vector, standard COMP-3/zoned
encodings, RFC 3398 cause mappings, and the textbook IBM HFP bit patterns.
The language bindings (COBOL, Fortran, Pascal, Ada) are built and run against
`libzxlegacy` by `bindings/build_lib.sh`.
