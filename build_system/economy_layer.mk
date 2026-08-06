# economy_layer.mk — the pirate-economy / governance / social / creative / platform
# modules, as one includable list. Mirrors platform_layer.mk.
#
# These 25 modules are pure, freestanding, integer C (surplus_real_t is Q32.32 on
# target; no libc/float/malloc). They are already wired into Makefile.arm64
# (which builds + boots them; the economy foundation self-checks at boot). To
# bring them up on x86_64 / riscv / arm32, an arch Makefile does two things:
#
#   1. include this fragment and append the vars:
#        include build_system/economy_layer.mk
#        KERNEL_SRCS += $(ECONOMY_LAYER_SRCS)
#        CFLAGS      += $(ECONOMY_LAYER_INC)
#
#   2. (optional) call boot_economy_init(<arch_uart_puts>) from that arch's
#      kernel_main, just before its BOOT_OK milestone, to roll the economy
#      foundation up at boot (as arch/arm64/kernel_main_arm64.c does).
#
# Every module host-tests against an external anchor (see `make verify-all` from
# kernel/) and compiles freestanding for aarch64/x86_64/armv7 under clang.
#
# DEPENDENCY NOTE: these build ON existing modules already in every arch's
# KERNEL_SRCS (finance/triple_ledger, surplus, robin_debanks/sha256+ed25519_verify,
# concord, chiglet, trispace, constellation, lightningrod, rational, tls/hkdf,
# identity, theme, icon, cards/sigil). If an arch build reports an undefined
# symbol, that base module is missing from that arch's KERNEL_SRCS, not here.

ECONOMY_LAYER_SRCS := \
    kernel/src/onepolicy/onepolicy.c \
    kernel/src/zcapital/zcapital.c \
    kernel/src/ipfs/ipfs.c \
    kernel/src/crown/crown.c \
    kernel/src/ministry/ministry.c \
    kernel/src/vino_stores/vino_stores.c \
    kernel/src/interspace/interspace.c \
    kernel/src/interspace/lex_rhodia.c \
    kernel/src/interspace/flagstate.c \
    kernel/src/interspace/federation.c \
    kernel/src/interspace/minister.c \
    kernel/src/broker/broker.c \
    kernel/src/battering_ram/battering_ram.c \
    kernel/src/finance_markets/finance_markets.c \
    kernel/src/iso20022/iso20022.c \
    kernel/src/alloc/alloc.c \
    kernel/src/pirate_fleet/pirate_fleet.c \
    kernel/src/social_spaces/social.c \
    kernel/src/reputation/reputation.c \
    kernel/src/art_studio/art_studio.c \
    kernel/src/games/games.c \
    kernel/src/voice/voice.c \
    kernel/src/subterm/subterm.c \
    kernel/src/orbital_compat/orbital_compat.c \
    kernel/src/fusion/fusion.c \
    kernel/src/sovereign_node/sovereign_node.c \
    kernel/src/logistics/logistics.c \
    kernel/src/pirate_apps/pirate_apps.c

ECONOMY_LAYER_INC := \
    -Ikernel/src/onepolicy -Ikernel/src/zcapital -Ikernel/src/ipfs \
    -Ikernel/src/crown -Ikernel/src/ministry -Ikernel/src/vino_stores \
    -Ikernel/src/interspace -Ikernel/src/broker -Ikernel/src/battering_ram \
    -Ikernel/src/tls -Ikernel/src/finance_markets -Ikernel/src/iso20022 \
    -Ikernel/src/alloc -Ikernel/src/pirate_fleet -Ikernel/src/social_spaces \
    -Ikernel/src/reputation -Ikernel/src/art_studio -Ikernel/src/games \
    -Ikernel/src/voice -Ikernel/src/subterm -Ikernel/src/orbital_compat \
    -Ikernel/src/fusion -Ikernel/src/sovereign_node -Ikernel/src/logistics \
    -Ikernel/src/pirate_apps
