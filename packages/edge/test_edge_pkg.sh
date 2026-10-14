#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_edge_pkg.sh TMPDIR — checks for the edge package that need no build:
# every profile validates, the launcher builds the expected zxvd command
# line and refuses bad or hostile configs, the rendered unit carries the
# profile's limits and the hardening, and systemd-analyze accepts it (when
# systemd-analyze is installed). One "[PASS] ..." per check; exit 1 on the
# first failure. The cross build and the qemu run are `make test-edge-smoke`.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
T="${1:?usage: test_edge_pkg.sh TMPDIR}"
rm -rf "$T"
mkdir -p "$T"
RUNP="$HERE/zxv-edge-run"
n=0
ok() { n=$((n + 1)); echo "[PASS] $1"; }
bad() { echo "[FAIL] $1"; exit 1; }

for p in rpi5 jetson minipc; do
    sh "$RUNP" --check "$HERE/profiles/$p.conf" >/dev/null || bad "profile $p validates"
    ok "profile $p validates"
done

# command line from a profile with a real models folder
mkdir -p "$T/models" "$T/run" "$T/state"
sed "s|^MODELS_DIR=.*|MODELS_DIR=$T/models|" "$HERE/profiles/rpi5.conf" >"$T/p.conf"
: >"$T/models/llama-3.2-1b-instruct-q4_k_m.gguf"
ZXV_EDGE_RUNTIME="$T/run" ZXV_EDGE_STATE="$T/state" sh "$RUNP" --print "$T/p.conf" >"$T/argv"
printf '%s\n' "$HERE/zxvd" --server --notify print --rpc "$T/run/zxvd.sock" --models-dir "$T/models" \
    --books "$T/state/books" --model "$T/models/llama-3.2-1b-instruct-q4_k_m.gguf" --net lan \
    --net-port 8723 >"$T/want"
cmp -s "$T/argv" "$T/want" || { diff "$T/want" "$T/argv" || true; bad "launcher command line"; }
ok "launcher: headless flags, socket in the runtime dir, books in state, LAN, no money"
: >"$T/models/qwen2.5-1.5b-instruct-q4_k_m.gguf"
ZXV_EDGE_RUNTIME="$T/run" ZXV_EDGE_STATE="$T/state" sh "$RUNP" --print "$T/p.conf" >"$T/argv"
[ "$(grep -c -- '^--model$' "$T/argv")" = 1 ] && grep -qx "$T/models/qwen2.5-1.5b-instruct-q4_k_m.gguf" "$T/argv" ||
    bad "first listed model that exists is the one loaded"
ok "first listed model that exists is the one loaded (one model slot)"
sed -e 's/^PEER_SERVE_PRICE=.*/PEER_SERVE_PRICE=25/' -e 's/^PEERS=.*/PEERS="192.168.1.20:8723 10.0.0.7:9000"/' \
    -e 's/^BOOKS=.*/BOOKS=memory/' "$T/p.conf" >"$T/q.conf"
ZXV_EDGE_RUNTIME="$T/run" sh "$RUNP" --print "$T/q.conf" | tr '\n' ' ' >"$T/argv"
grep -q -- "--books - " "$T/argv" && grep -q -- "--peer 192.168.1.20:8723 --peer 10.0.0.7:9000 --peer-serve 25" "$T/argv" ||
    { cat "$T/argv"; bad "peers, peer-serve and memory books"; }
ok "peers, memory books and peer-serve (only when a price is set)"
if ZXV_EDGE_RUNTIME="" RUNTIME_DIRECTORY="" sh "$RUNP" --print "$T/p.conf" >/dev/null 2>&1; then
    bad "no runtime directory is refused"
fi
ok "no runtime directory: refused outside systemd"

# hostile and malformed configs: each must be refused with exit 2
i=0
while IFS='|' read -r what line; do
    i=$((i + 1))
    case "$line" in
        DEL:*) key="${line#DEL:}"; grep -v "^$key=" "$T/p.conf" >"$T/bad$i.conf"; echo "$key=" >>"$T/bad$i.conf" ;;
        *) key="${line%%=*}"; grep -v "^$key=" "$T/p.conf" >"$T/bad$i.conf"; echo "$line" >>"$T/bad$i.conf" ;;
    esac
    rc=0
    ZXV_EDGE_RUNTIME="$T/run" ZXV_EDGE_STATE="$T/state" sh "$RUNP" --print "$T/bad$i.conf" >"$T/out$i" 2>&1 || rc=$?
    [ "$rc" = 2 ] || { cat "$T/out$i"; bad "refuses $what (rc $rc)"; }
    [ ! -e "$T/pwned" ] || bad "injection ran a command ($what)"
    ok "refuses $what"
done <<EOF
command substitution in MODELS|MODELS="\$(touch $T/pwned).gguf"
backticks in MODELS|MODELS="\`touch $T/pwned\`.gguf"
a second command after ;|NET=lan; touch $T/pwned
a pipe|PEERS=1.2.3.4:5|touch $T/pwned
a path in a model name|MODELS="../../etc/shadow.gguf"
a model that is not .gguf|MODELS="model.bin"
a hidden model file|MODELS=".x.gguf"
NET=wan|NET=wan
NET_PORT=0|NET_PORT=0
NET_PORT=65536|NET_PORT=65536
THREADS=0|THREADS=0
THREADS=257|THREADS=257
MEMORY_HIGH_MB equal to MEMORY_MAX_MB|MEMORY_HIGH_MB=6144
MEMORY_MAX_MB below 256|MEMORY_MAX_MB=255
a relative MODELS_DIR|MODELS_DIR=models
.. in MODELS_DIR|MODELS_DIR=/opt/../etc
a peer without a port|PEERS="10.0.0.1"
a peer with port 0|PEERS="10.0.0.1:0"
a peer that is a host name|PEERS="evil.example:8723"
a non-numeric PEER_SERVE_PRICE|PEER_SERVE_PRICE=-5
an unknown BOOKS mode|BOOKS=cloud
an unknown ARCH|ARCH=riscv64
an upper-case PROFILE|PROFILE=Rpi5
an unknown key|LD_PRELOAD=/tmp/x.so
a missing MEMORY_MAX_MB|DEL:MEMORY_MAX_MB
EOF
cat "$T/p.conf" >"$T/dup.conf"
echo "NET=off" >>"$T/dup.conf"
rc=0
sh "$RUNP" --check "$T/dup.conf" >/dev/null 2>&1 || rc=$?
[ "$rc" = 2 ] || bad "a key given twice is refused"
ok "refuses a key given twice"
echo "JUST TEXT" >>"$T/bad0.conf"
rc=0
sh "$RUNP" --check "$T/bad0.conf" >/dev/null 2>&1 || rc=$?
[ "$rc" = 2 ] || bad "a line without = is refused"
ok "refuses a line without ="

# the rendered unit for every profile
for p in rpi5 jetson minipc; do
    sh "$HERE/build_edge.sh" "$p" "$T/units" --unit-only
    U="$T/units/zxv-edge-$p.service"
    mx="$(sed -n 's/^MEMORY_MAX_MB=//p' "$HERE/profiles/$p.conf")"
    hi="$(sed -n 's/^MEMORY_HIGH_MB=//p' "$HERE/profiles/$p.conf")"
    th="$(sed -n 's/^THREADS=//p' "$HERE/profiles/$p.conf")"
    grep -qx "MemoryMax=${mx}M" "$U" && grep -qx "MemoryHigh=${hi}M" "$U" && grep -qx "CPUQuota=$((th * 100))%" "$U" ||
        bad "unit $p carries the profile limits"
    ok "unit $p: MemoryMax=${mx}M MemoryHigh=${hi}M CPUQuota=$((th * 100))%"
    for k in DynamicUser=yes ProtectSystem=strict NoNewPrivileges=yes ProtectHome=yes PrivateDevices=yes \
        MemoryDenyWriteExecute=yes CapabilityBoundingSet= RestrictAddressFamilies='AF_UNIX AF_INET AF_INET6' \
        SystemCallFilter=@system-service MemorySwapMax=0; do
        grep -qx "$k" "$U" || bad "unit $p has $k"
    done
    ok "unit $p: DynamicUser, ProtectSystem=strict, NoNewPrivileges and the rest of the hardening"
    if command -v systemd-analyze >/dev/null 2>&1; then
        sed "s|/opt/zxv-edge/bin/zxv-edge-run|$RUNP|" "$U" >"$T/units/v-$p.service"
        chmod +x "$RUNP" 2>/dev/null || true
        out="$(systemd-analyze verify "$T/units/v-$p.service" 2>&1)" || bad "systemd-analyze verify $p: $out"
        # drop what it says about the host's own units (a CI runner's
        # netplan-ovs-cleanup.service it may not read), keep the rest
        out="$(printf '%s\n' "$out" | grep -v '^[A-Za-z0-9@_.-]*\.\(service\|socket\|mount\|target\|timer\|path\|slice\): ' |
            grep -v '^$' || true)$(printf '%s\n' "$out" | grep "^v-$p\.service: " || true)"
        [ -z "$out" ] || bad "systemd-analyze verify $p warns: $out"
        ok "systemd-analyze verify $p: no errors, no warnings"
        sc="$(systemd-analyze security --offline=yes "$U" 2>/dev/null | sed -n 's/.*Overall exposure level for [^:]*: \([0-9.]*\).*/\1/p')"
        case "$sc" in 0.*|1.*) ok "systemd-analyze security $p: exposure $sc (below 2.0)" ;;
            *) bad "systemd-analyze security $p: exposure '$sc'" ;; esac
    else
        echo "[SKIP] systemd-analyze not installed"
    fi
done
sh -n "$HERE/build_edge.sh" && sh -n "$HERE/install.sh" && sh -n "$RUNP" || bad "shell syntax"
ok "build_edge.sh, install.sh and zxv-edge-run parse (sh -n)"
echo "$n checks passed"
