#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# app_sources.sh [srcs|incs|vinea|update|groups|groupincs|rpcdeps|rpcincs|modules|people|people_deps|people_incs] — the hosted app's C sources, one
# path per line, relative to kernel/. build_system/build_desktop.sh and
# kernel/arch/hosted/test_hosted.sh both read this list, so the shipped app
# and the tested app are built from the same files.
#
#   srcs    every source of zxv-host except the generated zxv_ui.c and the
#           forward-pass glue (added by the caller when zt_model.c exists)
#   incs    include directories (relative to kernel/), one per line
#   vinea   the Vinea node and what it needs (also used by the net test)
#   update  the update checker and what it needs
#   groups  zxvd only: the method groups (zxvd_ext.h) and the engines they
#           serve, after zxvd_groups.c; groupincs: their include directories
#   rpcdeps what zxvd_rpc.c links beyond web4, keccak and sha256, for the
#           unit tests that build it on its own; rpcincs: their includes
#   modules the module methods zxvd_rpc.c falls back to (zxvd_rpc_modules.c)
#           and what they link beyond web4_json, web4_util, keccak and
#           sha256: the one list zxvd_mod_archive.sh, build_zxvd.sh and
#           fuzz/Makefile read
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
    # AIPI over Vinea (zxv_net_host.h U12): the core, its adapter, the IPFS
    # node it fetches blocks through and the 64-state job tag
    echo src/aipi/aipi.c
    echo src/aipi/aipi_vna.c
    for f in ipfsn_multiformats ipfsn_unixfs ipfsn_store ipfsn_net ipfsn_ubh ipfsn_fidx; do
        echo "src/ipfs_node/$f.c"
    done
    echo src/lpres/lpres.c
    echo src/lpres/lpres_hex.c
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
                 zxv_update_host zx_notify_host zxv_answer_tag zxv_tiamat; do
            echo "arch/hosted/$f.c"
        done
        echo src/social/zx_notify.c
        for f in src/swarm/swarm_*.c; do echo "$f"; done
        for f in src/tensor/zt*.c; do echo "$f"; done
        # the settlement spine: swarm money on the ledger of record
        echo src/settle/settle.c
        # the 64-state tag on each answer and its quarantine log: src/lpres,
        # listed with the Vinea sources below (AIPI uses it too)
        # the node's self-audit of every posting (peer_audit layer 1)
        echo src/peer_audit/peer_audit.c
        echo src/peer_audit/pa_runtime.c
        # the owner's verified contributions from trades (quest)
        for f in quest quest_coop quest_guard; do echo "src/quest/$f.c"; done
        for f in pay_ledger pay_util pay_assure; do echo "src/pay/$f.c"; done
        # zxvd's jurisdiction host (zxvd_rpc.h J5) and the accounting names it reads
        echo src/juris/juris.c
        echo src/acct/acct.c
        # the Player Card's tiers (zxvd's player.card)
        echo src/sicily/sicily.c
        # one copy of each file: vinea and update share sha256, aead, hkdf, ubh,
        # event_envelope and the ML-DSA sources
        { vinea; update; } | awk '!seen[$0]++'
        ;;
    incs)
        # the last five are kernel/Makefile's CPATH
        for d in arch/hosted src/swarm src/tensor src/settle src/peer_audit src/quest src/pay src/sicily src/zcapital src/social src/vinea src/lpres src/robin_debanks src/juris \
                 src/edp_risk src/update src/ipfs_node src/pqsec src/mlkem include src/modbind \
                 src/e8 src/event_space src/surplus src/aipi; do
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
    groups)
        echo arch/hosted/zxvd_groups.c
        # cards.*: the charge-card issuer (its ML-DSA-65, keccak and sha256
        # are already in srcs)
        echo arch/hosted/zxvd_cards.c
        for f in cardnet cn_check cn_iso8583 cn_emv; do echo "src/cardnet/$f.c"; done
        # passkeys.*: ident's passkeys (id_core, id_passkey and the
        # post-quantum code they link are built for zxvd_pay in build_zxvd.sh)
        echo arch/hosted/zxvd_ident.c
        # the economy modules (zxvd_econ.c); zstats also reads grants and
        # vbond, which link but are not served. pay, sha256 and keccak are in srcs.
        echo arch/hosted/zxvd_econ.c
        for f in svault vswap vauction zenny zstats grants vbond; do echo "src/$f/$f.c"; done
        # economy groups (zxvd_sandbox.h): bonds, derivatives, grants, estate
        for f in zxvd_sandbox zxvd_instruments zxvd_estate; do echo "arch/hosted/$f.c"; done
        echo src/vderiv/vderiv.c
        for f in estate faraid recovery; do echo "src/estate/$f.c"; done
        # companion.*: the swarm's own DNA and emotion code is already in srcs
        echo arch/hosted/zxvd_companion.c
        # web4.*: the Web4 envelope feed on web4_agent (its ML-DSA-65, keccak,
        # sha256 and web4_json/web4_util are already linked) and freight
        echo arch/hosted/zxvd_web4.c
        echo src/web4/web4_agent.c
        for f in freight freight_ops; do echo "src/freight/$f.c"; done
        # seats.*: the people on this device and the login after boot; its
        # Argon2id (id_argon2.c) is added with ident in build_zxvd.sh
        echo arch/hosted/zxvd_seats.c
        echo src/seats/seats.c
        # power.*: the energy ledger and this machine's readers (docs/POWER.md)
        echo arch/hosted/zxvd_power.c
        echo arch/hosted/zxv_power_host.c
        echo src/power/power_ledger.c
        # irc.*: the IRC gateway (docs/IRC_GATEWAY.md), its protocol engine
        # and the repo's TLS 1.3 server and verifier (x25519, aead and hkdf
        # are in srcs; ML-DSA-87, keccak and sha256 too)
        echo arch/hosted/zxvd_irc.c
        echo arch/hosted/zxv_irc.c
        for f in server handshake record selfsigned ecdsa ecdsa_sign bn sha512 digest rsa x509 trust; do
            echo "src/tls/$f.c"
        done
        # pspace.*, hall.*: the net host and Vinea (vna_space) are in srcs
        echo arch/hosted/zxvd_pspace.c
        ;;
    groupincs)
        echo src/cardnet
        echo src/ident
        for d in svault vswap vauction zenny zstats; do echo "src/$d"; done
        for d in src/vbond src/vderiv src/grants src/estate src/web4 src/seats src/power src/tls src/vinea; do echo "$d"; done
        ;;
    rpcdeps)
        # the owner's files and spaces (zxvd_rpc.h F/S), the jurisdiction
        # host (J5) with the accounting names and the 64-state log it uses
        echo arch/hosted/zxv_files.c
        echo arch/hosted/zxv_spaces.c
        echo src/juris/juris.c
        echo src/acct/acct.c
        echo src/lpres/lpres_hex.c
        echo src/lpres/lpres.c
        ;;
    rpcincs) echo src/juris ;;
    modules)
        echo arch/hosted/zxvd_rpc_modules.c
        for f in freight/freight.c freight/freight_ops.c harmonic/zt_harmonic_wire.c \
                 stream/stream_seg.c ipfs_node/ipfsn_multiformats.c ehop/ehop_sched.c \
                 ehop/ehop_frame.c tls/aead.c web4/web4_cid.c web4/web4_agent.c pqsec/pq_mldsa65.c; do
            echo "src/$f"
        done
        for f in src/pqsec/mldsa/[a-z]*.c; do echo "$f"; done
        ;;
    *) echo "usage: $0 [srcs|incs|vinea|update|groups|groupincs|rpcdeps|rpcincs|modules|people|people_deps|people_incs]" >&2; exit 2 ;;
esac
