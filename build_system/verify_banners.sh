#!/bin/bash
# verify_banners.sh — a boot banner may only name a subsystem whose symbols are
# in the ELF. Phase 0 gate.
#
# WHY THIS EXISTS. Adding a file to the source list is NECESSARY BUT NOT
# SUFFICIENT. The arm64 AND x86_64 builds use -ffunction-sections -fdata-sections
# --gc-sections, so the linker DISCARDS any section nothing references. A module
# can compile, link, pass its tests, appear in the Makefile — and be absent from
# the binary.
# Measured 2026-08-09: tls/x25519/aead/record/handshake, oseq, zphi, e8, mixmat,
# modbind all had 0 symbols in kernel_arm64.elf immediately after being added to
# KERNEL_SRCS and a forced full rebuild. The .o files had 3/18/23/10 symbols each.
#
# So "linked" is not a claim about the build files. It is a claim about `nm`.
#
# WHICH ARCHES ACTUALLY COLLECT (measured 2026-08-11, read the LDFLAGS line, not
# a comment): ALL FIVE. arm64 always did, x86_64 since 2026-08-10, and riscv,
# riscv32 and arm32 since 2026-08-11 -- each of those three needed BOTH
# -ffunction-sections -fdata-sections in CFLAGS and -Wl,--gc-sections (bare
# --gc-sections on arm32, which links with ld directly) at the link.
#
# So "present" now means the same thing on every arch, and the symbol counts are
# finally comparable across a row. What that comparability first revealed:
#              BEFORE   AFTER    reachable
#   arm64        1211    1211      unchanged (already collecting)
#   x86_64        298     298      unchanged (already collecting)
#   riscv        2859     180      6.3%
#   riscv32      2861     188      6.6%
#   arm32        2853     118      4.1%
# The three "biggest" kernels in the old table were the three whose numbers
# meant nothing. Every cross-arch comparison printed before 2026-08-11 was
# comparing reached-code counts against linked-in-object counts.
#
# Those vanished symbols are STAGED MODULES, not dead code. They return the
# moment something calls them; being able to see the difference is the point.
set -u
ELF=${1:-kernel_arm64.elf}
[ -f "$ELF" ] || { echo "verify_banners: no $ELF — build first"; exit 2; }
fail=0
unaudited=0
check() {  # check <symbol-substring> <what the banner claims>
  # TEXT SYMBOLS ONLY. An earlier revision counted any nm line, so `grep -ci
  # vault` matched the BSS variables robin_vault and vault_key.86 and certified
  # "encrypted vault" while aes256_gcm.c had ZERO symbols in the ELF. A gate
  # that matches data where it needs code is worse than no gate -- it converts
  # an absence into a certification.
  n=$(nm "$ELF" 2>/dev/null | awk '$2 ~ /^[TtWi]$/ {print $3}' | grep -ci "$1")
  if [ "$n" -eq 0 ]; then
    printf "  ABSENT  %-14s  banner claims: %s\n" "$1" "$2"; fail=$((fail+1))
  else
    printf "  present %-14s  (%s syms)  %s\n" "$1" "$n" "$2"
  fi
}
echo "verify_banners: $ELF"
# PER-ARCH CHECK LISTS. A fixed list would be WRONG: 13 subsystems are arm64-only
# (syscall, zab, zxvfs_tri, display, zmedia, invproof, bombsquad, ...), so
# demanding them on riscv or arm32 would manufacture failures for capabilities
# those kernels never announce. That is the same defect as checking a symbol
# name the code does not export -- a verifier that cries wolf gets bypassed, and
# a bypassed gate protects nothing.
#
# Check the symbol prefix the code ACTUALLY exports, not the marketing name.
# Every arm64 entry below was confirmed against real `nm` output.
case "$ELF" in
  *arm64*)
    check x25519  "X25519 key agreement (RFC 7748)"
    check chg_    "Chiglet inference runtime"
    check oseq_   "causal ordering / happens-before"
    check mlkem   "post-quantum key establishment (ML-KEM-768)"
    # REMOVED, not silenced: aes256_gcm.c has 0 text symbols in the ELF, so
    # there is no encrypted vault to certify. The boot banner making this claim
    # must go too (kernel_main_arm64.c:965) -- see RESERVE_TRIAGE.md. Restore
    # this check when the cipher is genuinely wired.
    # check vault   "encrypted vault"
    check zxvfs_  "persistent filesystem"
    check hkdf    "TLS 1.3 key schedule"
    check aead    "TLS record AEAD (ChaCha20-Poly1305)"
    check e8_     "E8 lattice from the icosians"
    check zphi_   "exact Z[phi] golden integers"
    check mixmat_ "exact rational mixing matrices"
    check modbind_ "module construction rules"
    check ddna_   "phi-proportioned integrity checksum"
    check swarm_budget_ "[AI_OK] swarm Fibonacci budget cycle"
    check zt_rmsnorm    "[AI_OK] tensor forward step (zt)"
    check ai_boot_selfcheck "[AI_OK] swarm+tensor boot self-check"
    ;;
  *x86_64*)
    # PROMOTED OUT OF THE PROVISIONAL LIST, 2026-08-10. x86_64 now builds and
    # boots on the Linux box, so its banners were read from a real boot log and
    # every prefix below was confirmed against `nm kernel_x86_64.elf` on a link
    # that DOES pass --gc-sections. Nothing here is inferred from a directory
    # name; several entries exist precisely because the obvious guess is wrong:
    #   onepolicy/  exports op_*        (NOT onepolicy_*)
    #   zcapital/   exports zcap_*      (NOT zcapital_*)
    #   tolvovina/  exports tvl_*       (NOT tolvovina_*)
    #
    # mlkem IS DELIBERATELY NOT CHECKED HERE -- removed, not silenced. The old
    # PROVISIONAL branch demanded it of every non-arm64 ELF, and x86_64 passed
    # that check for as long as its link discarded nothing. The moment
    # --gc-sections went in, all 30 mlkem text symbols vanished and the gate
    # failed. It was a FALSE failure: kernel_main_x86_64.c prints no ML-KEM
    # banner at all (grep -i 'ml-kem|mlkem|post-quantum' on it is empty, and so
    # is the boot log). The gate was demanding evidence for a claim this kernel
    # never makes -- the "cries wolf" failure this file's own header warns
    # about. Add the check back on the day an x86_64 banner announces PQ key
    # establishment, at which point mlkem will also need a caller.
    check x86_ring3_ "ring-3 user mode (GDT/TSS/IDT + int 0x80)"
    check x86_exc_   "x86 exception vectors"
    check cell_fabric_ "Cellular Multikernel (CELL-001) fabric"
    check oseq_      "causal ordering / happens-before"
    check zxv_decl_  "declaration gate (provides/requires)"
    check modbind_   "module construction rules"
    check sha256     "SHA-256 (evidence + seal hashing)"
    check boot_evidence "boot evidence measurement chain"
    check e8_        "E8 lattice from the icosians"
    check zphi_      "exact Z[phi] golden integers"
    check tvl_       "TOL VOVINA UPAAH LOT geometry engine"
    check deploy_    "platform layer: deploy"
    check theme_     "platform layer: theme"
    check icon_      "platform layer: icon"
    check font_      "platform layer: font"
    check bridge_    "platform layer: web2/web3/web4 bridge"
    check upd_       "platform layer: decentralised update"
    check mage_      "platform layer: hat framework"
    check reality_   "platform layer: reality / sigil circuit"
    check op_symbiotic "economy: One Policy Symbiotic Maxim"
    check zcap_      "economy: nine capital forms"
    check crown_     "economy: Crown credential"
    check ministry_  "economy: Illumaheart treasury"
    check ipfs_      "economy: content-addressed spine"
    check swarm_budget_ "[AI_OK] swarm Fibonacci budget cycle"
    check zt_rmsnorm    "[AI_OK] tensor forward step (zt)"
    check ai_boot_selfcheck "[AI_OK] swarm+tensor boot self-check"
    ;;
  *riscv*)
    # PROMOTED OUT OF THE PROVISIONAL LIST, 2026-08-11, covering BOTH rv64 and
    # rv32: their banner sets were diffed and are IDENTICAL except for the two
    # lines that name the width ("xlen=64"/"xlen=32", "[RISC-V 64-bit]"/
    # "[RISC-V 32-bit]"), so one list serves both rather than two lists drifting.
    #
    # THE OLD `check mlkem` HERE WAS A FALSE FAILURE, and it is REMOVED, not
    # silenced. It passed for as long as these links discarded nothing. The
    # moment --gc-sections went in, all of mlkem's text symbols vanished and the
    # gate failed all three arches -- while grep -icE 'ml-kem|mlkem|post-quantum'
    # over kernel_main_riscv.c, kernel_main_arm32.c AND their real boot logs is
    # 0 on every one of them. Only arm64 announces PQ key establishment (and
    # passes). The gate was demanding evidence for a claim these kernels never
    # make: the "cries wolf" failure this file's own header warns about, and the
    # identical call already made for x86_64 on 2026-08-10.
    #
    # Every line below was read off the real boot log of a --gc-sections build
    # and confirmed against `nm`. The prefixes were then traced to the file that
    # DEFINES each symbol, because guessing a prefix from a directory name is
    # this project's most-repeated measurement error. The ones that would have
    # been guessed wrong here:
    #   hardware/rtl_device.c        exports rtl_        (NOT hardware_)
    #   holographic/holo.c           exports holo_       (NOT holographic_)
    #   net/jdr_piratenet.c          exports jdr_        (NOT net_)
    #   situation/situation_model.c  exports situation_  (NOT model_)
    #   choice/choice_core.c         exports choice_     (NOT core_)
    #   finance/ alone exports FOUR unrelated prefixes -- triple_ledger_,
    #     portfolio_, rail_, bridge_ -- one per banner, so a single finance_
    #     check would have certified all four from any one of them.
    check plic_        "PLIC interrupt controller"
    check riscv_timer_ "CLINT timer"
    check phase_coordinator_ "Phase Coordinator (EXEC_DC)"
    check rmag_        "RMAG"
    check lpres_       "LPRES"
    check iphase_      "IPHASE"
    check choice_      "CHOICE"
    check oseq_        "OSEQ / causal ordering"
    check edp_         "EDP risk calculus"
    check surplus_     "ISF surplus"
    check predictive_  "predictive model"
    check situation_   "tactical/strategic situation modeler"
    check triple_ledger_ "nine-capital triple ledger"
    check portfolio_   "financial instruments suite"
    check rail_        "Dragon/Phoenix/Thunderbird payment rails"
    check bridge_      "Web2-Web3 cryptocurrency bridge"
    check identity_    "universal national identity system"
    check jdr_         "JDR PirateNet harmonic hum carrier"
    check quantum_     "quantum + exotic matter devices"
    check rtl_         "hardware-as-code RTL device framework"
    check vino_        "Vino decentralized bank node"
    check vena_        "Vena application runtime"
    check holo_        "holographic data system"
    check zxv_decl_    "declaration gate (provides/requires)"
    check modbind_     "module construction rules"
    ;;
  *arm32*)
    # PROMOTED OUT OF THE PROVISIONAL LIST, 2026-08-11, same method: read off
    # this kernel's own boot log under a --gc-sections link, confirmed with
    # `nm`, prefixes traced to their defining files. See the *riscv* branch for
    # why `check mlkem` is gone.
    #
    # SHORTER THAN THE RISC-V LIST ON PURPOSE, and the difference is measured,
    # not assumed. kernel_main_arm32.c announces its M5 subsystems on one line
    # ("phase/rmag/lpres/iphase/choice/oseq") and prints NO PLIC, NO CLINT timer
    # and NO situation-modeler banner -- those are RISC-V arch banners. Checking
    # for them here would manufacture failures for capabilities this kernel
    # never claims, which is the same defect as the mlkem check being removed.
    check phase_coordinator_ "M5: phase coordinator"
    check rmag_        "M5: rmag"
    check lpres_       "M5: lpres"
    check iphase_      "M5: iphase"
    check choice_      "M5: choice"
    check oseq_        "M5: oseq"
    check edp_         "EDP operators + Fibonacci algebra"
    check surplus_     "ISF surplus"
    check predictive_  "predictive model"
    check triple_ledger_ "nine-capital triple ledger"
    check portfolio_   "financial instruments"
    check rail_        "payment rails"
    check bridge_      "crypto bridge"
    check identity_    "identity"
    check quantum_     "quantum devices"
    check rtl_         "RTL device framework"
    check jdr_         "JDR PirateNet"
    check vino_        "Vino bank"
    check vena_        "Vena runtime"
    check holo_        "holographic data"
    check zxv_decl_    "declaration gate (provides/requires)"
    check modbind_     "module construction rules"
    ;;
  *)
    # NO SILENT PASS FOR AN UNRECOGNISED ELF. Every arch this project builds now
    # has an audited list above, so falling through here means either a new
    # target arrived without one or the ELF name changed -- and in both cases a
    # gate that inspected NOTHING must never print PASS. A hollow green is worse
    # than no gate: it converts an absence into a certification, which is the
    # exact failure the `check` function's own comment was written about.
    echo "  ERROR: no audited banner list for '$ELF'."
    echo "         Build it, read its real boot banners, confirm each prefix"
    echo "         against nm (NEVER guess one from a directory name), and add"
    echo "         a branch above. Refusing to certify an ELF nothing checked."
    unaudited=1
    ;;
esac
echo
if [ "$unaudited" -eq 1 ]; then
  # Distinct exit path with its OWN wording. Reusing the "announced but absent"
  # message here would have been a small lie of exactly the kind this file
  # exists to stop: nothing was announced and nothing was absent -- no check ran
  # at all, which is a different (and worse) condition.
  echo "FAIL: $ELF was NOT CHECKED. No banner list matched its name."
  exit 1
fi
if [ "$fail" -gt 0 ]; then
  echo "FAIL: $fail subsystem(s) announced but absent from the ELF."
  echo "Either the banner must go, or the subsystem must be CALLED — being in"
  echo "the Makefile's source list is not enough. ALL FIVE arches collect"
  echo "sections as of 2026-08-11, so this failure means the same thing"
  echo "everywhere: no call chain reaches the module."
  echo "THIRD OPTION, and check it FIRST: the banner may not exist. Before"
  echo "giving a module a caller, grep the arch's kernel_main and its real boot"
  echo "log for the claim. A check for a banner this kernel never prints is a"
  echo "false failure, and a gate that cries wolf gets bypassed."
  exit 1
fi
echo "PASS: every checked banner is backed by symbols in the binary."
