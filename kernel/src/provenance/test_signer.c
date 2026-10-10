/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_signer.c — TEST-ONLY: the orlp Ed25519 signer (third_party/ed25519)
 * built against the field/group code that src/robin_debanks/ed25519_verify.c
 * already links. That file renames orlp's sc_reduce(s) to orlp_sc_reduce and
 * exports a two-argument sc_reduce shim, so the signer is pointed at the
 * original here. Hosted tests only; see test_signer.h. */
#define sc_reduce orlp_sc_reduce
#include "../../third_party/ed25519/src/sign.c"
#include "../../third_party/ed25519/src/keypair.c"
