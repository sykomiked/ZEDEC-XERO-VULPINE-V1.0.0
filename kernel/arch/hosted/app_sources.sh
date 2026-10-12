#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# app_sources.sh [srcs|incs|vinea|update|people|people_deps|people_incs] — the hosted app's C sources, one
# path per line, relative to kernel/. build_system/build_desktop.sh and
# kernel/arch/hosted/test_hosted.sh both read this list, so the shipped app
# and the tested app are built from the same files.
#
#   srcs    every source of zxv-host except the generated zxv_ui.c and the
#           forward-pass glue (added by the caller when zt_model.c exists)
#   incs    include directories (relative to kernel/), one per line
#   vinea   the Vinea node and what it needs (also used by the net test)
#   update  the update checker and what it needs
#   people  zxvd's people features (zxvd_people.c: social, calls, translation,
#           device mesh) and what they need beyond srcs; zxvd only
#   people_deps  what they use from srcs (tests that build without srcs)
#   people_incs  their include directories
set -eu
K="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$K"

vinea() {
    for f in src/vinea/vna_*.c; do echo "$f"; done
    # category 5: ML-DSA-87 identities, ML-KEM-1024 sessions (pqsec)
    echo src/pqsec/pq_mldsa87.c
    echo src/pqsec/pq_mlkem1024.c
    echo src/pqsec/mldsa/fips202.c
    echo src/mlkem/keccak.c
    echo src/tls/x25519.c
    echo src/tls/aead.c
    echo src/tls/hkdf.c
    echo src/robin_debanks/sha256.c
    echo src/ubh/ubh.c
    echo src/event_space/event_envelope.c
    echo src/zcapital/zcapital.c
}

update() {
    for f in zx_upcheck zx_ipns zx_upmanifest zx_upcheck_mldsa update; do
        echo "src/update/$f.c"
    done
    for f in ipfsn_dir ipfsn_multiformats ipfsn_unixfs ipfsn_store ipfsn_net ipfsn_ubh ipfsn_fidx; do
        echo "src/ipfs_node/$f.c"
    done
    echo src/robin_debanks/ed25519_verify.c
    for f in src/robin_debanks/sha256.c src/tls/aead.c src/tls/hkdf.c src/ubh/ubh.c \
             src/event_space/event_envelope.c src/pqsec/pq_mldsa65.c src/pqsec/mldsa/[a-z]*.c; do
        echo "$f"
    done
}

people() {
    echo arch/hosted/zxvd_people.c
    for f in social_post social_feed social_store social_bucket social_group social_sign_mldsa; do
        echo "src/social/$f.c"
    done
    echo src/reputation/reputation.c
    for f in call_rtp call_jitter call_cc call_fec call_mesh call_ice call_session; do
        echo "src/call/$f.c"
    done
    for f in xlate xlate_langid xlate_prompts; do echo "src/xlate/$f.c"; done
    for f in devmesh dm_sync dm_remote dm_capmkt; do echo "src/devmesh/$f.c"; done
    echo src/capmkt/capmkt.c
    # pq_matrix (devmesh identities): its hybrid layers and the SLH-DSA code
    for f in pq_matrix pq_mlkem1024 pq_mldsa87 pq_hqc5 pq_slh256s pq_slhdsa; do
        echo "src/pqsec/$f.c"
    done
    for f in src/pqsec/slhdsa/*.c; do echo "$f"; done
    # ML-KEM-768 (pq_matrix's STANDARD level)
    for f in mlkem768 mlkem_encode mlkem_sample mlkem_ntt mlkem_kpe; do echo "src/mlkem/$f.c"; done
}

# what the people features use from srcs (for a test built without srcs)
people_deps() {
    for f in src/robin_debanks/sha256.c src/mlkem/keccak.c src/pqsec/pq_mldsa65.c \
             src/pqsec/pq_mlkem1024.c src/pqsec/pq_mldsa87.c \
             src/pqsec/mldsa/[a-z]*.c src/tls/x25519.c src/tls/aead.c src/tensor/zt.c \
             src/tensor/zt_isf.c src/pay/pay_util.c src/pay/pay_assure.c src/web4/web4_json.c \
             src/web4/web4_util.c src/swarm/swarm_market.c src/swarm/swarm_budget.c \
             src/swarm/swarm_emotion.c; do
        echo "$f"
    done
}

case "${1:-srcs}" in
    srcs)
        for f in zxv_host zxv_http_guard zxv_model_host zxv_budget_gate zxv_net_host zxv_peer_host \
                 zxv_update_host zx_notify_host zxv_answer_tag; do
            echo "arch/hosted/$f.c"
        done
        echo src/social/zx_notify.c
        for f in src/swarm/swarm_*.c; do echo "$f"; done
        for f in src/tensor/zt*.c; do echo "$f"; done
        # the settlement spine: swarm money on the ledger of record
        echo src/settle/settle.c
        # the 64-state tag on each answer and its quarantine log
        echo src/lpres/lpres.c
        echo src/lpres/lpres_hex.c
        # the node's self-audit of every posting (peer_audit layer 1)
        echo src/peer_audit/peer_audit.c
        echo src/peer_audit/pa_runtime.c
        # the owner's verified contributions from trades (quest)
        for f in quest quest_coop quest_guard; do echo "src/quest/$f.c"; done
        for f in pay_ledger pay_util pay_assure; do echo "src/pay/$f.c"; done
        # the Player Card's tiers (zxvd's player.card)
        echo src/sicily/sicily.c
        # one copy of each file: vinea and update share sha256, aead, hkdf, ubh,
        # event_envelope and the ML-DSA sources
        { vinea; update; } | awk '!seen[$0]++'
        ;;
    incs)
        # the last five are kernel/Makefile's CPATH
        for d in arch/hosted src/swarm src/tensor src/settle src/peer_audit src/quest src/pay src/sicily src/zcapital src/social src/vinea src/lpres src/robin_debanks \
                 src/edp_risk src/update src/ipfs_node src/pqsec src/mlkem include src/modbind \
                 src/e8 src/event_space src/surplus; do
            echo "$d"
        done
        ;;
    people)
        # only what srcs does not already list, so no file is compiled twice
        have="$(sh "$K/arch/hosted/app_sources.sh" srcs)"
        people | awk '!seen[$0]++' | while read -r f; do
            printf '%s\n' "$have" | grep -qxF "$f" || echo "$f"
        done
        ;;
    people_deps) people_deps ;;
    people_incs)
        for d in src/call src/xlate src/devmesh src/capmkt src/reputation src/tls src/web4; do
            echo "$d"
        done
        ;;
    vinea) vinea ;;
    update) update ;;
    *) echo "usage: $0 [srcs|incs|vinea|update|people|people_deps|people_incs]" >&2; exit 2 ;;
esac
