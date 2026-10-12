#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# verify-local.sh — run CI's checks on this machine before a push, so a red
# check is caught in minutes instead of after a queue on GitHub. The commands
# are the ones .github/workflows/ci.yml runs; docs/CI.md maps each one to its
# job.
#
#   scripts/verify-local.sh                 fast tier: Apache-2.0 check,
#                                           clang-format on changed lines,
#                                           host tests (if code changed)
#   scripts/verify-local.sh --boot          + arm64 and x86_64 QEMU boots
#   scripts/verify-local.sh --san           + host tests under ASan/UBSan and
#                                           the integer sanitizer (the
#                                           minimum before marking a pull
#                                           request ready)
#   scripts/verify-local.sh --all           everything CI runs on Linux: the
#                                           above plus riscv/arm32 boots,
#                                           integer sanitizer, MSan, fuzz
#                                           smoke, cross-CPU KATs, coverage
#                                           and the mutation gate
#   scripts/verify-local.sh --docker ...    run inside the CI image
#                                           (scripts/ci.Dockerfile: Ubuntu
#                                           24.04, gcc 11, clang 18, every
#                                           cross compiler and QEMU). The
#                                           default on macOS.
#   scripts/verify-local.sh --install-hook  run the fast tier on every
#                                           `git push` (.git/hooks/pre-push)
#
# Other options:
#   --base REF   compare with REF to decide what changed (default: the merge
#                base with origin/main)
#   --force      run every selected check even if no code changed
#   --jobs N     parallel workers for the mutation gate (default: CPU count)
#
# Docker image platform: linux/amd64 by default, so results match GitHub's
# x86_64 runners (on Apple Silicon Docker Desktop runs it under Rosetta).
# ZXV_DOCKER_PLATFORM=linux/arm64 runs natively instead, faster but it tests
# the NEON paths rather than AVX2 and cannot build the x86_64 GRUB image.
#
# Exit status: 0 when every check that ran passed, 1 otherwise. A summary of
# each check and its time is printed at the end.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BOOT=0 SAN=0 ALL=0 DOCKER=0 FORCE=0 BASE="" JOBS=""
[ "$(uname -s)" = Darwin ] && DOCKER=1
[ "${ZXV_IN_DOCKER:-0}" = 1 ] && DOCKER=0
ARGS=("$@")
while [ $# -gt 0 ]; do
    case "$1" in
        --boot) BOOT=1 ;;
        --san) SAN=1 ;;
        --all) ALL=1 BOOT=1 SAN=1 ;;
        --docker) [ "${ZXV_IN_DOCKER:-0}" = 1 ] || DOCKER=1 ;;
        --no-docker) DOCKER=0 ;;
        --force) FORCE=1 ;;
        --base) BASE="${2:?--base needs a ref}"; shift ;;
        --jobs) JOBS="${2:?--jobs needs a number}"; shift ;;
        --install-hook)
            hook="$(git rev-parse --git-path hooks)/pre-push"
            printf '#!/bin/sh\n# installed by scripts/verify-local.sh --install-hook\nexec "%s/scripts/verify-local.sh"\n' "$ROOT" > "$hook"
            chmod +x "$hook"
            echo "pre-push hook installed: $hook (skip once with git push --no-verify)"
            exit 0 ;;
        -h|--help) sed -n '4,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

# ---- re-run inside the CI image -------------------------------------------
if [ "$DOCKER" = 1 ]; then
    command -v docker >/dev/null || { echo "docker not found; install Docker Desktop or pass --no-docker" >&2; exit 2; }
    PLATFORM="${ZXV_DOCKER_PLATFORM:-linux/amd64}"
    if command -v sha256sum >/dev/null; then SUM=$(sha256sum scripts/ci.Dockerfile); else SUM=$(shasum -a 256 scripts/ci.Dockerfile); fi
    TAG="zxv-ci:${SUM:0:12}-${PLATFORM##*/}"
    if ! docker image inspect "$TAG" >/dev/null 2>&1; then
        echo "building $TAG (once; later runs reuse it)"
        docker build --platform "$PLATFORM" -t "$TAG" -f scripts/ci.Dockerfile scripts || exit 2
    fi
    exec docker run --rm --platform "$PLATFORM" \
        -e ZXV_IN_DOCKER=1 -e CCACHE_DIR=/ccache -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
        -v zxv-ci-ccache:/ccache -v zxv-ci-cache:/root/.cache/zxv -v "$ROOT":/src -w /src \
        "$TAG" scripts/verify-local.sh "${ARGS[@]}"
fi

if [ "${ZXV_IN_DOCKER:-0}" = 1 ]; then
    git config --global --add safe.directory /src 2>/dev/null || true
fi

# ---- what changed ----------------------------------------------------------
if [ -z "$BASE" ]; then
    BASE=$(git merge-base origin/main HEAD 2>/dev/null || true)
fi
# Uncommitted and untracked files count too (WORKTREE=1).
eval "$(EVENT=push WORKTREE=1 build_system/ci_changes.sh "${BASE:-}" | grep -E '^(kernel|mac|dotnet|proofs)=')"
[ "$FORCE" = 1 ] && kernel=true
echo "base: ${BASE:-<none: everything>}   code changed: $kernel"

# ---- runner ---------------------------------------------------------------
RESULTS=() FAILED=0
step() {
    local name="$1"; shift
    local s e rc
    echo
    echo "=== $name ==="
    s=$(date +%s)
    ( "$@" )
    rc=$?
    e=$(date +%s)
    if [ $rc -eq 0 ]; then RESULTS+=("PASS  $(printf '%5ss' $((e - s)))  $name")
    else RESULTS+=("FAIL  $(printf '%5ss' $((e - s)))  $name"); FAILED=1; fi
}
skip() { RESULTS+=("skip          $1 ($2)"); }
have() { command -v "$1" >/dev/null 2>&1; }
TMP="$(mktemp -d)"
cleanup() {
    rm -rf "$TMP"
    # In the container the checks run as root; hand build outputs back to the
    # host user (matters on Linux hosts; Docker Desktop on macOS maps owners).
    if [ "${ZXV_IN_DOCKER:-0}" = 1 ] && [ "${HOST_UID:-0}" != 0 ]; then
        find /src -xdev -uid 0 -exec chown "$HOST_UID:${HOST_GID:-$HOST_UID}" {} + 2>/dev/null || true
    fi
}
trap cleanup EXIT

# gcc 11 is what CI's host-tests use; prefer it when installed.
if have gcc-11 && [ "$(gcc -dumpversion 2>/dev/null | cut -d. -f1)" != 11 ]; then
    mkdir -p "$TMP/gcc11"; ln -s "$(command -v gcc-11)" "$TMP/gcc11/gcc"; ln -s "$(command -v gcc-11)" "$TMP/gcc11/cc"
    export PATH="$TMP/gcc11:$PATH"
fi
if have ccache && [ -d /usr/lib/ccache ]; then CCACHE_PATH_DIR=/usr/lib/ccache; else CCACHE_PATH_DIR=""; fi

# Same wrapper the sanitizer jobs put first on PATH as gcc and cc.
san_path() {
    mkdir -p "$TMP/san"
    ln -sf "$ROOT/fuzz/tools/san-cc" "$TMP/san/gcc"
    ln -sf "$ROOT/fuzz/tools/san-cc" "$TMP/san/cc"
    echo "$TMP/san"
}

license() { python3 build_system/relicense_apache.py --check; }

lint() {
    local cf mb excl out
    cf=$(command -v clang-format) || { echo "clang-format not found (CI pins 18.1.8: pip install clang-format==18.1.8)"; return 1; }
    case "$("$cf" --version)" in *" 18.1.8"*) ;; *) echo "note: CI uses clang-format 18.1.8, this is $("$cf" --version)";; esac
    mb=${BASE:-$(git rev-list --max-parents=0 HEAD | tail -1)}
    set -f
    excl=$(grep -vE '^[[:space:]]*(#|$)' .clang-format-ignore | sed 's/^/:(exclude,glob)/')
    # without a second commit, git clang-format checks the working tree
    out=$(git clang-format --binary "$cf" --diff "$mb" -- 'kernel/*.c' 'kernel/*.h' $excl || true)
    set +f
    case "$out" in
        ""|"no modified files to format"|"clang-format did not modify any files") echo "formatting OK" ;;
        *) echo "$out"; return 1 ;;
    esac
}

# CI's host-tests job runs the strata check before verify-all; a module with
# no stratum fails it (and the boot builds' layer check).
host_tests() {
    build_system/verify_layers.sh && python3 tests/harness/test_verify_strata.py && make -C kernel verify-all
}

boot_one() {  # name makefile elf [marker]
    local name="$1" mk="$2"; shift 2
    [ -n "$CCACHE_PATH_DIR" ] && export PATH="$CCACHE_PATH_DIR:$PATH"
    make -f "$mk" clean >/dev/null && make -j"$(nproc)" -f "$mk" all && build_system/ci_boot_check.sh "$name" "$mk" 30 "$@"
}

rv32_boot() {
    local fw="$HOME/.cache/zxv/opensbi-rv32-v1.3.1/fw_dynamic.bin"
    if [ ! -f "$fw" ]; then
        git clone -q --depth 1 --branch v1.3.1 https://github.com/riscv-software-src/opensbi.git "$TMP/opensbi" &&
            make -C "$TMP/opensbi" -j"$(nproc)" PLATFORM=generic PLATFORM_RISCV_XLEN=32 CROSS_COMPILE=riscv64-linux-gnu- >/dev/null &&
            mkdir -p "$(dirname "$fw")" &&
            # riscv32_sbi.sh accepts a .bin only with its .elf beside it
            cp "$TMP"/opensbi/build/platform/generic/firmware/fw_dynamic.{bin,elf} "$(dirname "$fw")/" || return 1
    fi
    ZXV_RV32_SBI="$fw" boot_one riscv32 build_system/Makefile.riscv32
}

asan() {
    PATH="$(san_path):$PATH" SANCC_CC=clang ASAN_OPTIONS=detect_leaks=0 \
    SANCC_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fsanitize-ignorelist=$ROOT/fuzz/sanitizer-ignorelist.txt -Wno-error -Wno-unknown-warning-option -Wno-unused-command-line-argument" \
        make -C kernel verify-all
}

integer() {
    PATH="$(san_path):$PATH" SANCC_CC=clang \
    SANCC_MATCH='src/(pay|vino|rmag|rational|count_house|finance|zcapital|cbank|porter_house|surplus)/test_|src/swarm/test_swarm_economy' \
    SANCC_FLAGS="-fsanitize=integer -fno-sanitize-recover=all -fsanitize-ignorelist=$ROOT/fuzz/sanitizer-ignorelist.txt -Wno-error -Wno-unknown-warning-option -Wno-unused-command-line-argument" \
        make -C kernel verify-all &&
    make -C fuzz OUT=build/int CC_REPLAY=clang ED25519_GCC= PROP_ITERS=500 \
        REPLAY_SAN="-fsanitize=address,undefined,integer -fno-sanitize-recover=all -fsanitize-ignorelist=$ROOT/fuzz/sanitizer-ignorelist.txt" prop
}

msan() {
    make -C fuzz OUT=build/msan CC_REPLAY=clang ED25519_GCC= PROP_ITERS=300 \
        REPLAY_SAN="-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer -fno-sanitize-recover=all" replay prop &&
    fuzz/tools/msan_tests.sh
}

fuzz_smoke() { make -C fuzz replay prop PROP_ITERS=1000 && make -C fuzz smoke SMOKE_SECS=30; }

kat_cross() {
    KAT_CROSS_STRICT=1 make -C kernel kat-cross && make -C kernel ct-valgrind && make -C bench legacy
}

mutation() {
    python3 tests/harness/mutator/mutate.py --selftest &&
    python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --ci \
        --jobs "${JOBS:-$(nproc)}" --out "$TMP/mutation-results"
}

# ---- is the branch behind main? --------------------------------------------
# CI tests the pull request merged into the current main, not the branch on
# its own. A branch that is behind can pass here and fail there (or the
# other way round), so say so before spending time on the checks.
behind=$(git rev-list --count HEAD..origin/main 2>/dev/null || echo 0)
if [ "${behind:-0}" != 0 ]; then
    echo "WARNING: this branch is $behind commit(s) behind origin/main. CI tests the merge"
    echo "         with main: run 'git fetch origin main && git merge origin/main' first."
fi

# ---- the checks -------------------------------------------------------------
step "Apache-2.0 headers (quick-checks)" license
step "clang-format on changed lines (quick-checks)" lint
step "workflow draft tiers (quick-checks)" python3 build_system/check_workflow_tiers.py

if [ "$kernel" != true ]; then
    skip "host tests and everything below" "no code changed since $BASE; --force runs them"
else
    step "host tests: make -C kernel verify-all (host-tests)" host_tests
    if [ "$BOOT" = 1 ]; then
        if have aarch64-linux-gnu-gcc && have qemu-system-aarch64; then
            step "boot arm64 in QEMU (boot)" boot_one arm64 build_system/Makefile.arm64 "[AI_OK]"
        else skip "boot arm64" "needs gcc-aarch64-linux-gnu and qemu-system-arm, or --docker"; fi
        if have nasm && have qemu-system-x86_64 && have grub-mkrescue; then
            step "boot x86_64 in QEMU (boot)" boot_one x86_64 build_system/Makefile.x86_64 "[AI_OK]"
        else skip "boot x86_64" "needs nasm, grub-pc-bin, xorriso and qemu-system-x86, or --docker"; fi
    fi
    if [ "$SAN" = 1 ]; then
        if have clang; then
            step "host tests under ASan+UBSan (sanitize-asan-ubsan)" asan
            step "integer sanitizer (sanitize-integer)" integer
        else skip "ASan+UBSan and integer sanitizer" "needs clang"; fi
    fi
    if [ "$ALL" = 1 ]; then
        if have riscv64-linux-gnu-gcc && have qemu-system-riscv64; then
            step "boot riscv64 in QEMU (boot-extra)" boot_one riscv64 build_system/Makefile.riscv
            step "boot riscv32 in QEMU (boot-extra)" rv32_boot
        else skip "boot riscv64/riscv32" "needs gcc-riscv64-linux-gnu and qemu-system-misc, or --docker"; fi
        if have arm-linux-gnueabihf-gcc && have qemu-system-arm; then
            step "boot arm32 in QEMU (boot-extra)" boot_one arm32 build_system/Makefile.arm32
        else skip "boot arm32" "needs gcc-arm-linux-gnueabihf and qemu-system-arm, or --docker"; fi
        if have clang; then
            step "MemorySanitizer (sanitize-msan)" msan
            step "fuzz replay + 30 s per harness (fuzz-smoke)" fuzz_smoke
            step "coverage (coverage)" fuzz/tools/coverage.sh
        else skip "sanitizers, fuzzing and coverage" "need clang and llvm"; fi
        if have qemu-aarch64 && have valgrind; then step "cross-CPU KATs, ctgrind, legacy traces (kat-cross)" kat_cross
        else skip "kat-cross" "needs qemu-user, valgrind and the cross toolchains, or --docker"; fi
        step "mutation gate, all CI targets (mutation-subset)" mutation
    fi
fi

echo
echo "=== summary ==="
printf '%s\n' "${RESULTS[@]}"
if [ "$FAILED" = 0 ]; then echo "all checks that ran passed"; else echo "some checks FAILED"; fi
echo "not run here: macos-app (macOS only), dotnet-sdk, proofs, CodeQL"
exit "$FAILED"
