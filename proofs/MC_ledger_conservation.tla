---------------------- MODULE MC_ledger_conservation ----------------------
(* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC  *)
(* SPDX-License-Identifier: Apache-2.0                                    *)
(* The concrete account table TLC checks ledger_conservation against.     *)
(*                                                                        *)
(*   acct owner group  flags            meaning                           *)
(*   1    1     1      -                holder, ordinary form             *)
(*   2    2     1      FROZEN           holder under a compliance hold    *)
(*   3    3     1      ISSUER           issuer of group 1                 *)
(*   4    1     2      Crown            holder, Crown form                *)
(*   5    1     2      ISSUER, Crown    owner 1's own Crown issuance      *)
(*   6    2     2      Crown            another owner's Crown account     *)
(* Group 1 and group 2 stand for two different (asset, capital form)      *)
(* pairs, so cross-group balancing (L1 per group) is exercised.            *)
EXTENDS ledger_conservation

MCAccts == 1..6
MCOwner == <<1, 2, 3, 1, 1, 2>>  \* a function on 1..6
MCGroup == <<1, 1, 1, 2, 2, 2>>
MCIssuer == {3, 5}
MCFrozen == {2}
MCCrown == {4, 5, 6}
MCDeltas == -2..2
============================================================================
