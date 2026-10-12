/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxvd_pay.h — payments in zxvd: ISO 20022 messages, card check digits,
 * netting cycles, passkey checks and escrow holds on the owner's books. The
 * methods reach the app through zxvd's extension table (zxvd_rpc.h X1-X7).
 * Every book and message call goes through libzxv's frozen C ABI and the
 * books' root capability (zxvd_rpc.h M2); nothing here is put on the wire
 * but the results.
 *
 *   P1 MESSAGES. iso20022.render {"kind", "fields", "amounts"?, "entries"?,
 *      "tx_count"?} -> {"kind", "xml"}. kind is "pacs008" (pacs.008.001.09)
 *      or "camt053" (camt.053.001.08). fields is an object of strings:
 *      msg_id, cre_dt_tm, debtor_name, debtor_acct, creditor_name,
 *      creditor_acct, end_to_end_id, ccy, acct_id (libzxv says which a kind
 *      takes). amounts maps settlement, opening or closing to minor units
 *      (a JSON integer or a decimal string, M3); the decimal places are the
 *      ISO 4217 minor units of fields.ccy, which amounts therefore need.
 *      entries (camt053) is an array of {"amount_minor", "direction":
 *      "credit"|"debit", "status"?: "booked"|"pending"|"info"}. libzxv
 *      checks every value and the render (a camt053 must reconcile:
 *      opening + booked entries = closing); its refusals are -32000.
 *   P2 CARDS. card.check {"network": "phoenix"|"dragon"|"thunderbird",
 *      "pan"} -> {"network", "valid"}: the network's check digit (Damm,
 *      Luhn, Verhoeff). A PAN is 2..19 decimal digits (else -32000
 *      ZXV_E_ARG). Nothing is issued or authorized.
 *   P3 NETTING. libzxv's netting cycles (cb_net): one currency, no
 *      liquidity limit, no screening, in memory on a libzxv context of their
 *      own (they touch no account, so they need no books and outlive a books
 *      reopen). At most ZXVD_PAY_CYCLES cycles are held, open or closed;
 *      netting.drop frees one.
 *        netting.open      {"cycle", "currency"} -> a cycle
 *        netting.add       {"cycle", "participant"} -> a cycle
 *        netting.submit    {"cycle", "payment_id", "debtor", "creditor",
 *                          "amount_minor"} -> {"cycle", "payment_id",
 *                          "accepted": true}; idempotent on payment_id
 *        netting.close     {"cycle"} -> a cycle with its positions
 *        netting.positions {"cycle"} -> the same, once closed
 *        netting.list      {} -> {"cycles": [cycle]}
 *        netting.drop      {"cycle"} -> {"dropped"}
 *      A cycle is {"cycle", "currency", "closed", "participants": [code]}
 *      and, once closed, "positions": [{"participant", "net_minor"}] in the
 *      order added (+ receives, - pays) and "sum_minor": "0".
 *   P4 PASSKEYS. ident's WebAuthn-shaped relying-party check (ident.h,
 *      ML-DSA-65, COSE alg -49), in memory, at most ZXVD_PAY_PASSKEYS.
 *        passkey.register {"rp_id", "authenticator_data" (hex, with the
 *                         attested credential), "require_uv"?=true}
 *                         -> {"credential_id", "sign_count", "registered"}
 *        passkey.verify   {"rp_id", "credential_id", "authenticator_data"
 *                         (hex, 37 bytes), "client_data_hash" (hex, the
 *                         SHA-256 of clientDataJSON), "signature" (hex),
 *                         "require_uv"?=true} -> {"credential_id",
 *                         "verified": true, "sign_count"}
 *      A signature that does not verify, a counter that did not increase, a
 *      relying-party mismatch or a missing user-verified flag is -32020.
 *   P5 ESCROW on the owner's books. A hold moves funds from an account to
 *      the holder account "escrow: <CCY>" (opened with the first hold in
 *      that currency); release pays them to the payee, refund returns them
 *      to the payer. Each step is one ordinary posting, so the books file
 *      saves it and a restart replays it (zxvd_rpc.h F1-F6): the holds are
 *      read back from the escrow accounts' entries, never kept anywhere
 *      else. References: "escrow N to T" (hold N for account T),
 *      "escrow N release", "escrow N refund"; N counts up from 1 across
 *      every currency.
 *        ledger.hold    {"from", "to", "amount_minor"} -> a hold
 *        ledger.release {"hold"} -> a hold
 *        ledger.refund  {"hold"} -> a hold
 *        ledger.holds   {} -> {"holds": [hold], "escrow_accounts":
 *                       [{"account", "currency", "balance_minor",
 *                       "held_minor", "consistent"}], "unmatched"}
 *      A hold is {"hold", "state": "held"|"released"|"refunded", "from",
 *      "to", "amount_minor", "currency", "escrow_account", "entry_id",
 *      "closed_entry_id" (null while held), "releases_on": "owner"}.
 *      An escrow entry whose reference is not one of the three, or whose
 *      counterparty or amount does not match its hold, is counted in
 *      "unmatched"; "consistent" says the account holds exactly what its
 *      open holds add up to.
 *
 * iso20022.render (on the books' capability) and the ledger.* methods answer
 * -32603 "the books are not open" while the books are closed.
 *
 * HONEST LIMITS: messages are rendered, never sent (no SWIFT, PAPSS or RTGS
 * link). Netting cycles and passkeys are lost when zxvd stops. Escrow is
 * released or refunded only by the owner's own call: there are no release
 * conditions, no counterparty signature, no dispute and no quarantine yet.
 */
#ifndef ZXVD_PAY_H
#define ZXVD_PAY_H

#define ZXVD_E_PAY        (-32020) /* a payment step refused (the message says why) */
#define ZXVD_PAY_CYCLES   8u
#define ZXVD_PAY_PASSKEYS 32u
#define ZXVD_PAY_HEX_MAX  8192u /* hex digits in one parameter */

/* Register the methods with zxvd_rpc. 0, or -1 (no room in the table). */
int zxvd_pay_open(void);
/* Take the methods out, destroy every netting cycle and forget the
 * passkeys. Call it before the books close (zxvd_rpc_close). */
void zxvd_pay_close(void);

#endif /* ZXVD_PAY_H */
