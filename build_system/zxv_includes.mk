# zxv_includes.mk — THE ONE include set. Every architecture (x86_64,
# arm64, riscv64, riscv32, arm32, wasm32) includes this file and adds
# only its own kernel/arch/<arch> path. The two per-arch Makefiles
# previously carried separate lists that drifted; that duplication was
# the bug. One list, six targets: a header that resolves on one
# resolves on all, which is what cross-compiling cleanly requires.
#
# Generated from the tree; edit here, never in the per-arch Makefiles.

ZXV_INCLUDES := \
    -Ikernel/include -Ikernel/src -Ikernel/src/e8 -Ikernel/src/mixmat \
    -Ikernel/src/mbcomp -Ikernel/src/modbind -Ikernel/src/virtio -Ikernel/src/trispace \
    -Ikernel/src/holodeck -Ikernel/src/denconnect -Ikernel/src/mlkem -Ikernel/src/curzi \
    -Ikernel/src/pqsec -Ikernel/boot -Ikernel/src/plnp -Ikernel/src/smap \
    -Ikernel/src/decent -Ikernel/src/recon -Ikernel/src/hdcm -Ikernel/src/gematria \
    -Ikernel/src/dualtrack -Ikernel/src/superpos -Ikernel/src/pungent -Ikernel/src/ascent \
    -Ikernel/src/lattice -Ikernel/src/pterm -Ikernel/src/xedit -Ikernel/src/crypto_wallet \
    -Ikernel/src/nlb -Ikernel/src/license -Ikernel/src/surplus -Ikernel/src/edp_risk \
    -Ikernel/src/predictive -Ikernel/src/situation -Ikernel/src/rational -Ikernel/src/finance \
    -Ikernel/src/identity -Ikernel/src/quantum -Ikernel/src/hardware -Ikernel/src/audiogenomics_pro \
    -Ikernel/src/synthesis -Ikernel/compat -Ikernel/src/vino -Ikernel/src/vena \
    -Ikernel/src/holographic -Ikernel/src/axiom_matrix -Ikernel/src/telemetry -Ikernel/src/oseq \
    -Ikernel/src/rmag -Ikernel/src/lpres -Ikernel/src/iphase -Ikernel/src/choice \
    -Ikernel/src/phase_coord -Ikernel/src/crit168 -Ikernel/include/freestanding_stubs -Ikernel/src/net \
    -Ikernel/src/count_house -Ikernel/src/porter_house -Ikernel/src/event_sched -Ikernel/src/mesh_token \
    -Ikernel/src/community_chest -Ikernel/src/ai_layer -Ikernel/src/mesh_net -Ikernel/src/immigration \
    -Ikernel/src/robin_debanks -Ikernel/src/prism_break -Ikernel/src/vbe -Ikernel/src/desktop \
    -Ikernel/src/cellular_multikernel -Ikernel/src/event_space -Ikernel/src/orbital_elevator -Ikernel/src/orbital_compat \
    -Ikernel/src/constellation -Ikernel/src/event_transport -Ikernel/src/hypercube -Ikernel/src/yantra \
    -Ikernel/src/orbital_fabric -Ikernel/src/financial_fabric -Ikernel/src/finance_markets -Ikernel/src/sdk \
    -Ikernel/src/sdk_bridge -Ikernel/src/dual_space -Ikernel/src/zxvfs -Ikernel/src/zab \
    -Ikernel/src/invproof -Ikernel/src/codec -Ikernel/src/bombsquad -Ikernel/src/megarom \
    -Ikernel/src/display -Ikernel/src/syscall -Ikernel/src/loader -Ikernel/src/appkit \
    -Ikernel/src/shimmer -Ikernel/src/fractal -Ikernel/src/tolvovina -Ikernel/src/abacus \
    -Ikernel/src/video -Ikernel/src/swarm -Ikernel/src/lightningrod -Ikernel/src/bios \
    -Ikernel/src/dharma -Ikernel/src/sephirot -Ikernel/src/chiglet -Ikernel/src/cards \
    -Ikernel/src/refinery -Ikernel/src/concord -Ikernel/src/wyrmgate -Ikernel/src/wyverneye \
    -Ikernel/src/chronicle -Ikernel/src/deploy -Ikernel/src/theme -Ikernel/src/icon \
    -Ikernel/src/font -Ikernel/src/bridge -Ikernel/src/update -Ikernel/src/mage \
    -Ikernel/src/reality -Ikernel/src/bootfeat -Ikernel/src/onepolicy -Ikernel/src/zcapital \
    -Ikernel/src/ipfs -Ikernel/src/crown -Ikernel/src/ministry -Ikernel/src/vino_stores \
    -Ikernel/src/interspace -Ikernel/src/broker -Ikernel/src/battering_ram -Ikernel/src/tls \
    -Ikernel/src/iso20022 -Ikernel/src/alloc -Ikernel/src/pirate_fleet -Ikernel/src/social_spaces \
    -Ikernel/src/reputation -Ikernel/src/art_studio -Ikernel/src/games -Ikernel/src/voice \
    -Ikernel/src/subterm -Ikernel/src/fusion -Ikernel/src/sovereign_node -Ikernel/src/logistics \
    -Ikernel/src/pirate_apps -Ikernel/src/acpi -Ikernel/src/audio -Ikernel/src/bluetooth \
    -Ikernel/src/wifi -Ikernel/src/bootlegger -Ikernel/src/browser -Ikernel/src/clock \
    -Ikernel/src/dharana -Ikernel/src/epu -Ikernel/src/fs -Ikernel/src/legal_engine \
    -Ikernel/src/macgyver -Ikernel/src/panopticon -Ikernel/src/rce -Ikernel/src/rur \
    -Ikernel/src/sutra -Ikernel/src/ubh -Ikernel/src/zxpkg -Ikernel/src/sched \
    -Igui
