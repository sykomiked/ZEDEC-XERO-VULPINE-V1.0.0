/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* pay_rails.h — THE canonical numerics and jurisdiction codes of the three
 * Vino rails. Every module that names a rail defines its own macro as one of
 * these (pay_ledger.h PAY_RAIL_*_CODE, vino_stores.h VINO_ISO_*,
 * iso20022.h ISO_CCY_*, cbank/cb_vss.h CB_RAIL_*, cardnet.h CN_RAIL_*,
 * devmesh.h DM_RAIL_*, evolve/evo.h EVO_RAIL_*, finance/capital_forms.h
 * RAIL_FINANCIAL/PROVENANCE/EXTERNALITY), so the numbers cannot diverge;
 * test_rails_canon.c checks each of them and the runtime mappings.
 *
 *   rail    numeric  jurisdiction  meaning
 *   DEBIT   555      NCR           what an account holds (asset / backing)
 *   CREDIT  777      NRE           what an account owes (claim / liability)
 *   EQUITY  888      PNS           debit - credit (live equity)
 *
 * 555, 777 and 888 are unassigned in ISO 4217: they are platform-internal
 * numerics, not currencies, and conventional parsers reject them. NCR, NRE
 * and PNS are platform jurisdiction codes (New California Republic, Neo
 * Roman Empire, Principality of New Sicily), NOT ISO 3166 country codes,
 * and travel only in /ZXV/ fields. The cbank resolution designator 811 is a
 * procedure class, not a rail, and is deliberately not listed here.
 *
 * finance/capital_forms.h historically names 777 "PROVENANCE" and 888
 * "EXTERNALITY" (the triple-ledger book each rail feeds); those names are
 * aliases of CREDIT and EQUITY, not other rails.
 *
 * Dependency-free: include it by relative path from any module.
 */
#ifndef ZXV_PAY_RAILS_H
#define ZXV_PAY_RAILS_H

#define ZXV_RAIL_CODE_DEBIT  555u
#define ZXV_RAIL_CODE_CREDIT 777u
#define ZXV_RAIL_CODE_EQUITY 888u

#define ZXV_RAIL_CODE_DEBIT_JURIS  "NCR" /* New California Republic (not ISO 3166)    */
#define ZXV_RAIL_CODE_CREDIT_JURIS "NRE" /* Neo Roman Empire (not ISO 3166)           */
#define ZXV_RAIL_CODE_EQUITY_JURIS "PNS" /* Principality of New Sicily (not ISO 3166) */

/* True iff `code` is one of the three rail numerics. */
#define ZXV_RAIL_CODE_IS_RAIL(code)                                                                \
    ((code) == ZXV_RAIL_CODE_DEBIT || (code) == ZXV_RAIL_CODE_CREDIT ||                            \
     (code) == ZXV_RAIL_CODE_EQUITY)

#endif /* ZXV_PAY_RAILS_H */
