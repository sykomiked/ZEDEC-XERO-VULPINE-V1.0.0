#!/bin/bash
# verify_layers.sh — DRC for the kernel. Four checks, all mechanical.
#
# In silicon nobody eyeballs metal spacing; a design-rule checker does it. The
# layer discipline needs the same treatment, because conventions in this tree
# have a measured survival time of HOURS -- ROADMAP_TO_COMPLETE.md contradicted
# SERVER_HANDOVER.md within a day of being written.
#
#   1 LAYER      a module may not include a header from a HIGHER layer
#   2 ACYCLIC    the requires-graph must have no cycle (a cycle never boots)
#   3 PROVIDED   every REQUIRES must be PROVIDED by something
#   4 CONTRACT   two providers of one capability must agree on version
#
# Checks 2-4 are enforced at runtime by modbind_verify_graph(); this is the
# static half, so a violation fails the BUILD rather than the boot.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2
fail=0

# Layer map. A module not listed is UNASSIGNED -- reported, not failed, because
# assigning 278 files by guess would be worse than admitting the gap.
layer_of() {
  case "$1" in
    # sha256 is a PURE FUNCTION over bytes with no dependencies -- substrate,
    # not trust. L3 is where POLICY about hashes lives (capabilities,
    # verification, signing); the primitive itself belongs beside zphi and rat.
    # The DRC found this on its first run: alloc(L2), zxvfs_tri(L2) and
    # invproof(L1) all include it, and all three were correct. The layer
    # assignment was wrong, not the code.
    zphi|rat|rational|e8|mixmat|surplus|crit168|sha256) echo 0 ;;
    # --- L0 additions, 2026-08-10 (the 49 newly-built orphans) ---
    # rmag is the Rational Magnitude Engine: quota arithmetic over
    # rational_t {int64_t num,den} from m5_types.h. Measured, not assumed --
    # rmag_core.c includes only m5_types.h (kernel/include, unowned) and its own
    # header, and rmag.c only rmag.h. Pure arithmetic depending on nothing is
    # the definition of L0, the same seat as rat and zphi.
    #   This assignment is the load-bearing one in this batch. sephirot was
    # already L0 and includes rmag_core.h; dharana, and six of the nine dharma
    # sources, include it too. With rmag unassigned all of those edges were
    # SKIPPED and their layer checks were vacuous -- exactly the vacuity this
    # file already called out for upaah. Assigning it makes them real, and they
    # pass (0 > 0 is false; 5 > 0 is false).
    # dharana is 112 gate instances over rmag quotas; its own header is explicit
    # that the naming is symbolic and "the mathematics underneath is ordinary
    # and checkable". A pure gate array over L0 arithmetic is L0.
    # ubh is the UBH-168 framed envelope: reversible bit slicings of 168 bits
    # into octets/septets/sextets, no dependencies at all. Same reasoning that
    # puts sha256 here -- a pure, total function over bytes is substrate.
    # crypto_verify is HMAC-SHA256 (RFC 2104) plus a constant-time compare, and
    # includes nothing but sha256.h. It is classified per-FILE, not by its
    # robin_debanks directory, because that directory mixes primitives with the
    # banking module; this follows the precedent set for sha256 directly above.
    rmag|dharana|ubh|crypto_verify)              echo 0 ;;
    oseq|identity|invproof)                      echo 1 ;;
    # --- L1 additions ---
    # event_clock (dir: clock) is NOT the L4 timer chip, and reading it as one
    # would be the classic name-based error. Its own header: the kernel "runs on
    # its own internal event sequence (ordinal-based, not time-based) and only
    # syncs to wall-clock time when an external interface requires it." That is
    # ordinal causality -- oseq's row -- not a device. The 8254 `timer` at L4 is
    # the chip; this is the ordering discipline above nothing but arithmetic.
    # lpres (K3) attests source, dialect, dependencies, runtime, symbols and
    # CONTRADICTORY EVIDENCE under paraconsistent logic. Evidence and witnesses
    # are where invproof already sits. It is not L3: it grants no capability and
    # includes no crypto -- measured, lpres.c includes only lpres.h.
    # choice (K5) resolves competing candidates deterministically with explicit
    # tie-break and defers the ambiguous to S0. Determinising a decision is the
    # same discipline as determinising an order, and it too depends on nothing.
    clock|lpres|choice)                          echo 1 ;;
    mm|alloc|zxvfs)                              echo 2 ;;
    # --- L2 addition ---
    # fs (O2) is transactional storage: commit/rollback, snapshots, recovery --
    # zxvfs's row. It is tri-space storage, so the zxvfs/zxvfs_tri split above
    # invites reading it as L3; it is not, and the difference was measured
    # rather than argued: zxvfs_tri is L3 because it INCLUDES zab.h to gate
    # execution, and fs.c includes no capability header at all. It persists; it
    # does not decide who may.
    fs)                                          echo 2 ;;
    # NOTE: zxvfs_tri is NOT L2. Plain zxvfs is storage; zxvfs_tri is
    # CAPABILITY-GATED storage -- it calls zab to decide who may execute a
    # triad, which makes it a trust-layer module that happens to persist. The
    # DRC caught this by finding it including zab.h from L2. The distinction is
    # real and I had collapsed it; per-FILE classification is needed here
    # because one directory holds modules at two different layers.
    zxvfs_tri)                                   echo 3 ;;
    mlkem|zab|modbind|tls|decent)                echo 3 ;;
    # zxv_decl_gate is the DECLARATION-GRAPH BOOT GATE extracted out of
    # kernel/arch/arm64/kernel_main_arm64.c so all five arch mains can run it.
    # Named per-FILE even though its directory (modbind) already resolves to
    # L3: an unnamed basename falls through to the directory only by luck of
    # placement, and a file that moved once can move again into a directory
    # with no assignment, where the DRC would skip it in silence.
    zxv_decl_gate)                               echo 3 ;;
    # --- L3 addition ---
    # phase_coord (K6) "admits, vetoes, or defers coupled steps and hardware
    # actions under coverage and health policy. It ISSUES ADMISSION TOKENS."
    # A token that gates whether an action may proceed is a capability, which
    # is zab's row. It must sit BELOW the things it gates, and it does:
    # phase_coord.c includes only its own header.
    phase_coord)                                 echo 3 ;;
    virtio|video|input|net)                      echo 4 ;;
    # virtio_gpu is named per-FILE even though its directory (virtio) already
    # resolves to L4, for the reason zxv_decl_gate states above: an unnamed
    # basename inherits its directory only by luck of placement, and a file that
    # moves once can move again into a directory with no assignment, where the
    # DRC would skip it in silence. L4 is also a real ceiling on it and not a
    # courtesy -- it must NOT include display.h (L5), because the geometry
    # travels the other way here: virtio-gpu REPORTS a mode, it does not consult
    # the negotiator for one. An include of display.h would fire this check, and
    # that is the intended outcome.
    virtio_gpu)                                  echo 4 ;;
    # --- L4 additions: four device stacks and one route registry ---
    # acpi parses firmware tables to enumerate what hardware exists -- the same
    # job pci does on the x86 side, and pci is L4 below.
    # audio is the stream plus software mixer sitting on the sound device that
    # arch/arm64/virtio_snd.c drives; it is the third member of the row the
    # 13-layer table names with framebuffer and input.
    # bluetooth is the HOST half of a BR/EDR stack -- everything above the HCI
    # transport boundary and nothing below it, per its own header. wifi is the
    # 802.11 STA/AP subsystem. Both are NIC-side stacks beside net.
    # iphase (K4) is the odd one and is placed on evidence, not on its name: it
    # keeps an endpoint registry and route tables with priority/weight and
    # failover chains for "events, sessions, media, tunnels, hardware paths".
    # It is a route CONTRACT registry, net/m5route's row -- and it drives no
    # hardware itself (measured: iphase.c includes only iphase.h), so L4 is a
    # real ceiling on it, not a courtesy.
    acpi|audio|bluetooth|wifi|iphase)            echo 4 ;;
    # The x86 port-I/O device cluster, re-homed into Makefile.x86_64. L4 is
    # "devices" and all six are exactly that: ata is a block device, keyboard
    # and mouse are the concrete modules the abstract `input` entry above only
    # names, pci is the device-discovery bus, pic is the 8259 and timer the
    # 8254 -- platform chips, addressed by port like any other device.
    #
    # Assigning them L4 rather than lower is the STRICTER choice and that is
    # why it was made: the DRC only fires on an include from a HIGHER layer, so
    # a low assignment would quietly license anything to depend on them. At L4
    # the cluster's real edges (keyboard/mouse/timer -> pic) are same-layer and
    # legal, while a future L2 module reaching for pic.h is caught.
    #
    # idt and gdt are deliberately NOT assigned here. Both are 32-bit
    # protected-mode modules under separate adjudication (8-byte gates, a
    # uint32_t descriptor base, .code32 stubs); classifying them is that
    # adjudication's call, not this one's. See PROVENANCE/X86_REHOME.md.
    ata|keyboard|mouse|pci|pic|timer)            echo 4 ;;
    # blockdev is the block-device abstraction (blockdev.c: the registry/vtable
    # in kernel/src/blockdev) together with its shared, arch-neutral RAM-backed
    # provider ramdisk.c -- a GENUINE block device that reads and writes a
    # static RAM buffer and PROVIDES(blockdev_ready) for the four non-arm64
    # arches (arm64 keeps its own arch/arm64/ramdisk.c; alternative provision is
    # legal). A block device is L4 "devices", the same row the x86 port-I/O
    # `ata` block device on the line above sits on. Keyed on the DIRECTORY so
    # both blockdev.c and ramdisk.c are covered -- both were silently UNASSIGNED
    # until now, so their layer checks were vacuous. Measured safe at L4:
    # ramdisk.c's only kernel/src include is its own blockdev/ramdisk.h; its
    # other two includes (blockdev.h, zxv_barrier.h) live in kernel/include,
    # which is unowned and skipped, so it has no upward edge to fire this check.
    blockdev)                                    echo 4 ;;
    display|codec|bombsquad|rce|chiglet)         echo 5 ;;
    # --- L5 additions ---
    # panopticon (and panopticon_vpn) watch incoming connections and rate who is
    # watching the user. This is bombsquad's argument verbatim, from the layer
    # doc: "it monitors margins OF OTHER SUBSYSTEMS, so it cannot precede them.
    # It watches; it is not foundational." Same shape, same layer.
    # epu is placed here BECAUSE OF ITS OWN HEADER, which says in a box: "THIS
    # IS A MODEL. IT IS NOT A DEVICE DRIVER. There is no EPU silicon." So the
    # L4 reading its directory name invites is refused in writing by the file
    # itself. It is a model other subsystems query -- chiglet's seat, and
    # chiglet is on this line already.
    # dharma (the whole directory bar upaah, which stays L0 per-file below) is
    # the karma set-ring: bodhi, chakra, dharma, karma, mantra, naga_raja,
    # tantra, uvn, yantra. Its floor is forced and not chosen -- dharma.c
    # includes phase_coordinator.h (L3) and lpres_core.h (L1), so it cannot sit
    # below 3. It is placed at 5 rather than 3 on meaning: dharma.h calls itself
    # "the bridge between kernel logic and OS-layer logic", it consumes L3
    # admission and L0 quota arithmetic, and what it offers -- consequence
    # accounting other subsystems read -- is a service.
    #   NOTE dharma/yantra.c takes this DIRECTORY assignment; it must not be
    # given a per-file `yantra` case, because kernel/src/yantra (yantra_fabric)
    # is a different module and per-file matching is by bare basename.
    # emu is assigned as a WHOLE DIRECTORY, deliberately, and this is the one
    # place in this batch where a directory is classified rather than a file.
    # sms.c is the only emu source being added, but a per-file `sms` case would
    # have been VACUOUS: every header it includes is emu-owned, so with emu
    # unassigned each edge resolves to -1 and is skipped and the check proves
    # nothing -- the same vacuity this file already calls out for upaah.
    #   This is NOT a guess from the directory name, and the first attempt at it
    # was WRONG. emu was put at L9 on meaning -- a console core executes a
    # foreign program and megarom binds cartridges, which reads exactly like the
    # "program fusion / orbital compat" row -- and the DRC refuted it on the
    # first run: tvl_raster.c and tvl_stereo.c are L7 SURFACE and both include
    # emu/holo.h. Something the surface reaches into is below the surface, not
    # above it. Measuring every cross-directory edge in the directory gives a
    # hard interval instead of an opinion: the highest ASSIGNED thing emu
    # includes is chiglet (L5, via break_potency, console_trajectory,
    # game_universe, megarom_synth, netplay, render_lineage, story_mechanics),
    # plus surplus (L0) and four unassigned modules; and tolvovina bounds it at
    # <= 7 from above. So emu is in [5,7], and 5 is where its content actually
    # sits: interpreters and renderers that consume devices and hand a running
    # program to the surface, which is the row display and codec are on.
    # Classifying it makes 36 files checkable, and they pass.
    panopticon|epu|dharma|emu)                   echo 5 ;;
    vino|vino_stores|finance_markets|license|iso20022|crown|ministry) echo 6 ;;
    desktop|shell|apps|sutra|appkit|xedit)       echo 7 ;;
    # --- L7 addition ---
    # browser: tabs, a back/forward stack, bookmarks, an HTML tokenizer and a
    # block layout engine. It is a thing the user looks at, which is the whole
    # content of "surface"; it goes on the row apps and xedit are already on.
    browser)                                     echo 7 ;;
    # --- L8, a row that had no members until now ---
    # bootlegger is native P2P transport: direct-stream peer routing and data
    # sync with no central choke point. THIRTEEN_LAYERS.md's L8 row is "remote
    # surface, P2P mesh, netplay" and this is literally that.
    bootlegger)                                  echo 8 ;;
    # --- L9 additions: composition ---
    # zxpkg is named in the L9 row of THIRTEEN_LAYERS.md by hand; it is the
    # on-disk package as a tri-space triad.
    # macgyver compiles tri-space source artifacts atomically through Forge /
    # Counterforge / Mediatrix and emits signed container triads -- producing
    # installable artifacts is the same row as packaging them.
    # rur is the typed bridge from source-language frontends to ZXV Event IR:
    # a compiler front-end contract, which is composition, not substrate. Its
    # own header is careful that it "is not a second runtime authority and is
    # not directly privileged by the kernel", which rules out anything lower.
    zxpkg|macgyver|rur)                          echo 9 ;;
    # --- L11 addition ---
    # legal_engine (and legal_engine_ext) generate compliance architecture,
    # treaty templates and user agreements -- instruments of policy, the row
    # crown / ministry / concord occupy. It is NOT L12: L12 commons is the
    # relation BETWEEN sovereign systems (interspace, federation), whereas this
    # drafts the instruments a single sovereign issues.
    legal_engine)                                echo 11 ;;
    # TOL VOVINA UPAAH LOT. Per-FILE, because one directory holds modules at two
    # different layers -- the same situation zxvfs/zxvfs_tri is in above.
    #   tvl_geom / tvl_raster / tvl_stereo are SURFACE (L7): the orientation
    #   frame, the rasteriser and the complementary-channel stereo are what the
    #   user actually looks at, alongside desktop/shell/appkit.
    #   tvl_rom is COMPOSITION (L9): a TVUL container binds world, mechanics,
    #   sutra, assets and orientation into one bootable cartridge and registers
    #   it with megarom -- it composes surface modules, so it must sit above
    #   them or it could not legally include them.
    #   tvl_bringup is the roll-call that CALLS all four, so it is L9 too.
    tvl_geom|tvl_raster|tvl_stereo)              echo 7 ;;
    tvl_rom|tvl_bringup)                         echo 9 ;;
    tolvovina)                                   echo 7 ;;
    # UPAAH / VPAAH / PIR -- the phase-7 interface engine. Per-FILE, because
    # dharma/ holds eight other staged modules that are NOT compiled and whose
    # layers are not established; naming the directory would classify them by
    # guess. "Phase 7" is an L13 phase number, not a DRC layer: measured by
    # dependency, upaah.c calls nothing. It includes m5_types.h (kernel/include,
    # outside kernel/src, so unowned) and uses sephirot.h for ENUMS ONLY -- it
    # calls no sephirot function, which is why sephirot.c is not built alongside
    # it. A pure function over trits with no module dependencies is substrate,
    # the same reasoning that puts sha256 at L0 above.
    #   sephirot is L0 for the same reason and to make upaah's check REAL: with
    #   sephirot unassigned, upaah's only resolvable include was skipped and the
    #   layer check on it would have been vacuous. sephirot.h includes only
    #   m5_types.h; sephirot.c (not built on arm64) includes rmag_core.h, and
    #   rmag is itself unassigned, so this adds no new edge today.
    upaah|sephirot)                              echo 0 ;;
    # The other 15 of the 49 newly-built sources needed no new case: they land
    # on an assignment that already existed, and they are listed here so the
    # claim "every added file is assigned" can be audited rather than trusted.
    #   crit168/crit168_os.c        -> crit168 L0
    #   robin_debanks/crypto_verify -> per-file L0 above
    #   sephirot/sephirot.c         -> per-file L0 above
    #   mlkem/genomic_codon.c       -> mlkem   L3
    #   net/dtmf.c, net/radio.c     -> net     L4
    #   video/video.c               -> video   L4
    #   rce/rce_units.c             -> rce     L5
    #   apps/apps.c                 -> apps    L7
    #   desktop/desktop.c           -> desktop L7
    #   sutra/sutra_{capital,chiglet,lexer,parser,rails,runtime,selfaudit}.c
    #                               -> sutra   L7
    # rce_units and sutra are not coincidences: THIRTEEN_LAYERS.md names
    # rce_units in its L5 row and sutra in its L7 row by hand.
    *) echo -1 ;;
  esac
}

echo "verify_layers: DRC over kernel/src"
unassigned=0
while IFS= read -r f; do
  fb=$(basename "$f" .c)
  ml=$(layer_of "$fb")                      # per-file first
  [ "$ml" = "-1" ] && ml=$(layer_of "$(basename "$(dirname "$f")")")
  [ "$ml" = "-1" ] && { unassigned=$((unassigned+1)); continue; }
  # every local include, resolved to its owning directory
  grep -oE '#include[[:space:]]*"[^"]+\.h"' "$f" 2>/dev/null |
  sed 's/.*"\(.*\)"/\1/' | while IFS= read -r inc; do
    base=$(basename "$inc" .h)
    # find which module directory actually owns that header
    owner=$(find kernel/src -name "$base.h" -maxdepth 3 2>/dev/null | head -1)
    [ -z "$owner" ] && continue
    om=$(basename "$(dirname "$owner")")
    # Resolve the INCLUDED header the same way the includer was resolved:
    # per-FILE first, then its owning directory. Until 2026-08-10 this side used
    # the directory only, and the asymmetry was a live bug rather than a
    # theoretical one -- adding `dharma` as a directory made the DRC report
    # "upaah.c (L0) includes upaah.h (L5)", i.e. a module accused of depending
    # upward on ITSELF, purely because its own per-file L0 was ignored on the
    # includee side. Every per-file case in layer_of exists precisely because
    # one directory holds modules at two layers (zxvfs vs zxvfs_tri, tolvovina's
    # L7/L9 split, sha256 inside robin_debanks); reading only the directory
    # threw that information away exactly where it mattered. Making it symmetric
    # also STRENGTHENS the check: an include of zxvfs_tri.h now correctly
    # reports L3 rather than zxvfs's L2, and sha256.h now resolves to L0 instead
    # of being skipped with its unassigned parent directory.
    ol=$(layer_of "$base")
    [ "$ol" = "-1" ] && ol=$(layer_of "$om")
    [ "$ol" = "-1" ] && continue
    if [ "$ol" -gt "$ml" ]; then
      echo "  LAYER VIOLATION  $f (L$ml) includes $inc (L$ol)"
      echo "violation" >> /tmp/vl_fail.$$
    fi
  done
done < <(find kernel/src -name '*.c' ! -name 'test_*' ! -name 'debug_*' 2>/dev/null)

[ -f /tmp/vl_fail.$$ ] && { fail=$(wc -l < /tmp/vl_fail.$$); rm -f /tmp/vl_fail.$$; }
echo "  unassigned modules (reported, not failed): $unassigned"
echo
if [ "${fail:-0}" -gt 0 ]; then
  echo "FAIL: $fail layer violation(s). A module may not depend upward."
  exit 1
fi
echo "PASS: no module includes from a higher layer."
echo "NOTE: checks 2-4 (acyclic / provided / contract) run at boot via"
echo "      modbind_verify_graph(). Static enforcement of those needs the"
echo "      PROVIDES/REQUIRES declarations to exist in source first."
