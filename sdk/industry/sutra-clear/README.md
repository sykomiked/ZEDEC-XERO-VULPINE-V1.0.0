<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Sutra-Clear (PAPSS-Core): multilateral clearing in exact amounts

Sutra-Clear is a C11 library for clearing houses and central-bank engineers.
A house collects payment instructions during a cycle, then closes the cycle.
At the close it:

- nets the instructions multilaterally per currency, in exact minor units;
- charges the assurance fee;
- settles the net positions through a hub account on the libzxv ledger;
- renders the settlement as ISO 20022;
- writes one signed, hash-chained clearing-cycle record.

[Tri-Witness](../tri-witness/) replays those records offline.

Sutra-Clear is **designed to support PAPSS and SWIFT ISO 20022 formats**.
It is not connected to PAPSS, SWIFT, Afreximbank, any central bank or any
payment system, and none of them has endorsed or certified it. It moves
money only on the libzxv ledger it is given. It never settles over amateur
radio. The API is [`include/zsclear.h`](include/zsclear.h).

## Instructions

An instruction (`zsc_instr`) has these fields:

- a message id and an end-to-end id;
- a creation time;
- a currency and an amount in minor units;
- the debtor and creditor agents (participant codes);
- the debtor's and creditor's names and accounts.

It arrives in one of three ways:

| Entry | Call | Source |
|---|---|---|
| the API | `zsc_submit` | `ZSC_SRC_API` |
| ISO 20022 pacs.008 | `zsc_submit_pacs008` (`zsc_parse_pacs008` on its own is pure) | `ZSC_SRC_PACS008` |
| a Sutra NET treaty, as text or as the 2,542-byte COBOL copybook record | `zsc_submit_sutra`, `zsc_submit_record` (the ABI 1.5 treaty group) | `ZSC_SRC_SUTRA`, `ZSC_SRC_RECORD` |

Each instruction is checked on its own:

- the ids are 1..35 bytes of printable ASCII;
- the time is ISO 8601 (`YYYY-MM-DDThh:mm:ss`, then optionally 1..6
  fraction digits, then optionally `Z` or `±hh:mm`);
- the currency is a payable ISO 4217 currency with minor units, enabled in
  the house;
- the amount is 1..2^53-1 minor units;
- the agents are two different registered participants;
- the names are 1..70 bytes of strict UTF-8, with no control characters
  and no leading or trailing space;
- the accounts are 1..34 bytes of printable ASCII.

A treaty is checked whole before any of its obligations is submitted. The
treaty must be a NET treaty in a house currency, and every party must be a
participant whose deposit and claim are both 0. OWE line *k* becomes the
instruction `"<treaty name>-<k>"`.

### The pacs.008 subset

`zsc_parse_pacs008` reads exactly this shape, at most 16,384 bytes, and
nothing else (`ZSC_E_SYNTAX` with the byte offset, or `ZSC_E_FIELD` for a
bad value):

```
[<?xml version="1.0" encoding="UTF-8"?>]
<Document xmlns="urn:iso:std:iso:20022:tech:xsd:pacs.008.001.09">
 <FIToFICstmrCdtTrf>
  <GrpHdr> <MsgId/> <CreDtTm/> <NbOfTxs>1</NbOfTxs>
           <SttlmInf><SttlmMtd>CLRG</SttlmMtd></SttlmInf> </GrpHdr>
  <CdtTrfTxInf>
   <PmtId><EndToEndId/></PmtId>
   <IntrBkSttlmAmt Ccy="XXX">1234.56</IntrBkSttlmAmt>
   [<InstdAmt Ccy="XXX">…</InstdAmt>]      equal to the settlement amount
   <ChrgBr>SLEV</ChrgBr>
   <Dbtr><Nm/></Dbtr> <DbtrAcct><Id><Othr><Id/></Othr></Id></DbtrAcct>
   <DbtrAgt><FinInstnId> <BICFI/> | <ClrSysMmbId><MmbId/></ClrSysMmbId> </FinInstnId></DbtrAgt>
   <CdtrAgt> (the same) </CdtrAgt>
   <Cdtr><Nm/></Cdtr> <CdtrAcct><Id><Othr><Id/></Othr></Id></CdtrAcct>
   [<InstdAmt …/>]                          here, if not above
  </CdtTrfTxInf>
 </FIToFICstmrCdtTrf>
</Document>
```

The parser also enforces these rules:

- Whitespace is allowed only between elements.
- Text may use only the five predefined XML entities.
- Comments, CDATA, processing instructions and attributes other than
  `Ccy` are refused.
- An amount has no sign, no leading zeros and no more decimals than the
  currency's minor units.
- An 8- or 11-character agent is a BIC; any other agent is a clearing
  member id.

`zsc_render_pacs008` writes the same shape. It uses libzxv's pacs.008
builder (`zxv_msg_render`) and adds the two agent elements, for which the
builder has no field.

## Conflicts and operators

Each submission is classified like this:

| Submission | Result |
|---|---|
| a new end-to-end id | accepted (pending) |
| the same id and the same content (message id and time aside) | `ZIND_E_REPLAY` |
| the same id with different content | `ZIND_E_HELD`: **every** version leaves the cycle and is held as BOTH, logged to the quarantine log |
| a new version of a held id | added to the group (up to 4) |
| an id settled, decided or withdrawn in any earlier cycle | `ZIND_E_REPLAY`, logged as Forbidden |

A registered operator settles a held group by signing a canonical message
(`zsc_decision_message`). The signature uses ML-DSA-87 with context
`"sutra-clear/operator"`. The message is:

```
"sutra-clear/decision/v1" || house key id || e2e || u8 choice ||
group digest (the version digests, arrival order) || chosen version's digest
```

Choice 0 rejects every version. Choice *k* settles version *k* in the open
cycle and rejects the rest. Rejected versions are logged as Forbidden.
`zsc_resolve` refuses:

- an unregistered key (`ZSC_E_OPERATOR`);
- a bad signature (`ZXV_E_VERIFY`, logged as tampering);
- a replayed decision (`ZXV_E_NOT_FOUND`).

A group stays held across cycles until it is decided. `zsc_withdraw` takes
a pending instruction out of the cycle (for example after `ZXV_E_FUNDS`).
Its id then counts as decided.

## The close

`zsc_cycle_close` runs these steps in order:

1. **Order.** Pending instructions are sorted by end-to-end id.
2. **Netting, per currency.** Each participant's net position is the sum of
   its credits minus its debits.
   - The ABI 1.5 netting group (`zxv_netting_*`) is a stub
     (`ZXV_FEAT_NETTING` clear), so the house nets through the treaty
     engine instead: `zxv_treaty_run` with `OP_NET`, in batches of at most
     16 parties and 64 obligations.
   - The batch results are added with checked int64 and cross-checked
     against the plain sum. Any overflow is `ZXV_E_RANGE`; nothing wraps.
   - `zsc_use_netting(h, ctx)` switches to `zxv_netting_*` once a library
     reports the feature.
3. **The assurance fee.** For each participant and currency, the debtor
   pays `zxv_fee_assure_carry(rem, gross)` per instruction, in order. The
   remainder carries to its next fee, across cycles. `zxv_fee_split` splits
   the total 50/25/15/10 across the reserve, dividend, infrastructure and
   regeneration buckets. The kit never re-derives the formula.
4. **Funds, all or nothing.** Each participant must cover its net debit and
   then its fee from its prefunded account (`<house>/<code>/<ccy>`). Every
   resulting balance, hub total and bucket total must stay within
   2^53-1. If either check fails, **nothing is posted** and the cycle stays
   open: `ZXV_E_FUNDS` names the participant, otherwise `ZXV_E_RANGE`.
5. **Settlement, through the house's capability.** All postings for a
   currency are made in this fixed order, with references
   `<house>/<cycle>/<ccy>/<participant index>/S|R|F0..F3`:
   - net debtors pay the hub (`<house>/hub/<ccy>`);
   - the hub pays net creditors, and returns to zero;
   - each fee share goes to its bucket (`<house>/<bucket>/<ccy>`).

   If a posting fails after the checks passed, which only another writer on
   the same ledger can cause, the house stops (`ZSC_E_BROKEN`) and the
   event is logged.
6. **ISO 20022.** One pacs.008 is rendered for each settlement posting, and
   one camt.053 for each participant statement (opening balance, entries,
   closing balance), all through `zxv_msg_*`.
7. **The record.** The cycle record below is written into the house's
   chain (`zind_chain`, domain `"sutra-clear"`). Each record is signed with
   ML-DSA-87 and hash-linked with SHA3-256. A record holds at most 8,000
   bytes of the cycle record, so a long cycle record is split across
   several chain records:

   ```
   payload = u32 cycle no | u16 index | u16 count | up to 8,000 bytes
   ```

## The clearing-cycle record ("ZSC1")

Little-endian. `str` is a u8 length followed by the bytes. The instruction
`wire` is: u8 source, str msg id, str creation time, str e2e, str ccy, u64
amount, then str debtor agent, creditor agent, debtor name, debtor account,
creditor name, creditor account.

```
"ZSC1" | str house id | house key id[32] | u32 cycle no | str cycle id |
str settle time | u8 backend (1 zxv_netting, 2 treaty engine) |
u8 n, n × (str ccy, u8 minor units) | u16 n, n × str participant code |
u32 n, n × wire                         instructions settled, by e2e
u32 n, n × (str e2e, u8 k, k × wire)    groups still held at the close
u32 n, n × (str e2e, u8 choice, group[32], chosen[32], operator key id[32],
            signature[4627])            decisions applied in the cycle
u32 n, n × wire                         instructions withdrawn
u32 n, n × (u8 ccy, u16 participant, i64 net)       active participants
u32 n, n × (u8 ccy, u16 participant, u64 gross, rem in, fee, rem out,
            split[4])                                debtors
u32 n, n × (u8 kind, u8 ccy, u16 participant, u8 bucket, i64 amount,
            u64 entry id, str from account, str to account, str ref)
u32 n, n × (str account, u8 ccy, i64 opening, i64 closing)
            per currency: active participants, the hub, the four buckets
u64 first entry | u64 last entry | u64 ledger head | journal digest[32] |
u32 n, n × (u8 kind, str message id, SHA3-256 of the XML[32])
```

The journal digest is `zind_subject("sutra-clear/journal", …)` over
`"ZSJP1"`, followed by one item per ledger posting with an id up to the
head:

```
u64 id | str from account | str to account | str ccy | u64 amount | str ref
```

`zsc_journal_export` writes the ledger journal that Tri-Witness reads
("ZSJ1"):

```
"ZSJ1" | u32 n accounts, n × (u32 id, str name, str ccy, u8 kind) |
u64 n postings, n × (u64 id, u32 from, u32 to, u64 amount, str ref)
```

## Example

`make demo` runs [`examples/clearing_demo.c`](examples/clearing_demo.c).
The demo house has four banks and two currencies. It takes three pacs.008
messages, a conflicting resend that is held until an operator signs, a
damaged message refused at its byte, and a Sutra treaty from a COBOL shop.
It then closes the cycle and prints positions, fees, the first settlement
pacs.008 and the export sizes.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/sutra-clear test        # 2,901 checks, fuzz smoke, demo, no-float
make -C sdk/industry/sutra-clear SANITIZE=1 OUT=build/san test
make -C sdk/industry/sutra-clear fuzz FUZZ_ITERS=20000
```

`test_sclear` covers these areas:

- **Field checks.**
- **The pacs.008 subset.**
  - Round trips through the renderer.
  - Every prefix of a valid message is refused.
  - 46 mutations (markup in text, entities, CDATA, a conversion, extra
    transactions, bad amounts), each refused at the right byte.
- **Netting.**
  - Hand-computed positions and fees.
  - 400 random multi-currency cycles against the plain sum.
  - 40 participants and 600 obligations, which crosses the 16-party and
    64-obligation batch limits.
  - An int64 overflow, refused.
- **Funds, ranges and withdrawals.**
  - The all-or-nothing funds check, and a fee shortfall.
  - A position or hub total beyond 2^53-1.
  - Withdrawals.
- **Conflicts and decisions.**
  - Held groups across cycles.
  - Forged, unregistered, replayed and out-of-range decisions.
- **Treaties.**
  - Sutra text and copybook records.
  - Ten refused treaties.
- **Faults and capabilities.**
  - Another writer breaking a cycle (`ZSC_E_BROKEN`, with nothing more
    posted).
  - A capability without attestation, and a revoked one.
- **Outputs.** The journal export, the ISO output and the chain.

`fuzz_sclear` sends random traffic through a small pool of end-to-end ids,
so conflicts and replays are common. The traffic includes:

- pacs.008 messages with random damage;
- treaties;
- genuine and forged decisions;
- funding, withdrawals and closes.

After every close the checks below must hold. The output is byte-identical
under gcc and clang.

- Positions sum to zero.
- Hubs return to zero.
- Money is conserved per currency.
- Splits and carries are correct.
- `zxv_ledger_check` passes.
- The chain verifies.

## Limits

- **Formats, not connectivity.** The kit reads and writes a documented
  subset of pacs.008.001.09 and writes camt.053 through libzxv. It does
  not speak to PAPSS, SWIFT or any network, and has no partnership with,
  endorsement from or certification by any of them, or any central bank.
  It makes no compliance claim (AML, sanctions, settlement finality,
  PFMI). Those belong to whoever deploys it.
- **No amateur radio.** Settlement never goes over amateur radio, and
  nothing in the kit sends anything.
- **No FX.** Each currency nets on its own. An `InstdAmt` that differs
  from the settlement amount is refused.
- **Prefunded, hub-and-spoke.** Participants must hold their net debit
  and their fee before the close. No credit lines, no partial settlement,
  no queueing. A shortfall stops the whole cycle until it is funded or
  instructions are withdrawn.
- **One writer.** The house assumes it is the only writer of its accounts.
  Another writer can only stop it (`ZSC_E_BROKEN`), never corrupt it.
- **The ABI 1.5 gaps.** The kit works around three gaps in the 1.5 library:
  - `zxv_netting_*` is a stub (`ZXV_E_NOTIMPL`, `ZXV_FEAT_NETTING` clear)
    with no capability form, so netting goes through the treaty engine.
  - libzxv's pacs.008 builder has no agent fields, so the kit inserts
    `DbtrAgt` and `CdtrAgt` itself.
  - pacs.009 is not implemented in 1.5, so settlement legs between
    institutions are rendered as pacs.008 with the house as the
    counterparty.
- **Time is given, not kept.** The kit has no clock. Creation and
  settlement times are the caller's.
- **Signing is hedged.** ML-DSA-87 signatures differ from run to run, so
  exports are not byte-identical across runs. Their content and every
  digest in them are.
