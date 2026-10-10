#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_signed_app.sh — signing, in two unrelated senses.
#
#   build_system/build_signed_app.sh
#       (no arguments; the original job) build the sample ZSP app, sign it
#       under the root Ed25519 key, and regenerate the embeddable header
#       (root pubkey + signed .zsp). The root private key lives at
#       build_system/keys/root_priv.pem and is generated once if absent. In
#       a real release it is held offline (HSM / air-gapped); it is NOT
#       required to BUILD the kernel, only to (re)sign the app.
#
#   build_system/build_signed_app.sh --mac-app path/to/ZXV.app
#   build_system/build_signed_app.sh --mac-dmg path/to/ZXV-<v>-macos.dmg
#       Apple code signing for the macOS app: codesign with the Hardened
#       Runtime, a secure timestamp and macos/ZXV.entitlements; submit to
#       Apple's notary service with notarytool; staple the ticket; verify
#       with codesign and spctl. Called by build_desktop.sh. Everything is
#       driven by environment variables, and each stage is skipped cleanly
#       (exit 0, with a line saying so) when its variables are absent:
#
#         ZXV_SIGN_IDENTITY   "Developer ID Application: <name> (<TEAMID>)",
#                             or its SHA-1 hash. Absent: the app is
#                             ad-hoc signed (runs locally; Gatekeeper will
#                             warn on other Macs) and nothing is notarised.
#         ZXV_TEAM_ID         optional; when set, the identity must belong
#                             to this team (catches a wrong certificate).
#         ZXV_KEYCHAIN        optional keychain file holding the identity.
#         ZXV_NOTARY_PROFILE  a notarytool keychain profile, made once with
#                               xcrun notarytool store-credentials <profile>
#                             or, for CI, an App Store Connect API key:
#         ZXV_NOTARY_KEY      path to the AuthKey_<id>.p8 file
#         ZXV_NOTARY_KEY_ID   its key id
#         ZXV_NOTARY_ISSUER   its issuer id
#                             Absent: signed but not notarised.
#
#       No key, certificate or password is ever written by this script, and
#       it never reads or writes build_system/keys/ in these modes.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)

# ===================================================================== macOS

log() { printf '  %s\n' "$*"; }

mac_preflight() {   # -> 0 when Apple tools exist, else says why and returns 1
    if [ "$(uname -s)" != Darwin ] || ! command -v codesign >/dev/null 2>&1; then
        log "code signing skipped: not a Mac (codesign is macOS only)"
        return 1
    fi
    return 0
}

identity_checked() {
    [ -n "${ZXV_SIGN_IDENTITY:-}" ] || return 1
    if [ -n "${ZXV_TEAM_ID:-}" ] && [[ "$ZXV_SIGN_IDENTITY" != *"($ZXV_TEAM_ID)"* ]] &&
       ! [[ "$ZXV_SIGN_IDENTITY" =~ ^[0-9A-Fa-f]{40}$ ]]; then
        echo "ZXV_SIGN_IDENTITY does not belong to team $ZXV_TEAM_ID" >&2
        exit 1
    fi
    return 0
}

codesign_one() {    # path [extra codesign args...]
    local p="$1"; shift
    local kc=()
    [ -n "${ZXV_KEYCHAIN:-}" ] && kc=(--keychain "$ZXV_KEYCHAIN")
    codesign --force --timestamp --options runtime ${kc[@]+"${kc[@]}"} \
        --sign "$ZXV_SIGN_IDENTITY" "$@" "$p"
}

notary_args() {     # prints nothing and returns 1 when notarisation is off
    if [ -n "${ZXV_NOTARY_PROFILE:-}" ]; then
        printf '%s\n' --keychain-profile "$ZXV_NOTARY_PROFILE"
    elif [ -n "${ZXV_NOTARY_KEY:-}" ] && [ -n "${ZXV_NOTARY_KEY_ID:-}" ] &&
         [ -n "${ZXV_NOTARY_ISSUER:-}" ]; then
        printf '%s\n' --key "$ZXV_NOTARY_KEY" --key-id "$ZXV_NOTARY_KEY_ID" \
            --issuer "$ZXV_NOTARY_ISSUER"
    else
        return 1
    fi
}

notarize() {        # file-to-submit thing-to-staple
    local submit="$1" staple="$2" args=() line out id
    while IFS= read -r line; do args+=("$line"); done < <(notary_args) || true
    if [ ${#args[@]} -eq 0 ]; then
        log "notarisation skipped: set ZXV_NOTARY_PROFILE (or the ZXV_NOTARY_KEY trio)"
        return 0
    fi
    log "submitting $(basename "$submit") to Apple's notary service (this takes minutes)"
    out=$(xcrun notarytool submit "$submit" "${args[@]}" --wait --output-format json)
    id=$(printf '%s' "$out" | sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' | head -1)
    if ! printf '%s' "$out" | grep -q '"status" *: *"Accepted"'; then
        echo "notarisation failed: $out" >&2
        [ -n "$id" ] && xcrun notarytool log "$id" "${args[@]}" >&2 || true
        exit 1
    fi
    log "notarised (submission $id)"
    xcrun stapler staple "$staple"
    xcrun stapler validate "$staple"
}

mac_app() {
    local app="$1"
    [ -d "$app/Contents/MacOS" ] || { echo "not an app bundle: $app" >&2; exit 2; }
    mac_preflight || return 0
    local ent="$here/macos/ZXV.entitlements"
    if ! identity_checked; then
        # ad-hoc: the bundle's seal matches its contents, so it runs here;
        # it is not a Developer ID signature and is never notarised
        codesign --force --sign - --options runtime --entitlements "$ent" \
            "$app/Contents/MacOS/zxv-engine"
        codesign --force --sign - --options runtime --entitlements "$ent" "$app"
        log "ad-hoc signed (set ZXV_SIGN_IDENTITY for a Developer ID signature)"
        return 0
    fi
    # inside out: the helper first, then the bundle (no --deep)
    codesign_one "$app/Contents/MacOS/zxv-engine" --entitlements "$ent"
    codesign_one "$app" --entitlements "$ent"
    codesign --verify --strict --deep --verbose=2 "$app"
    log "signed with the Hardened Runtime: $ZXV_SIGN_IDENTITY"

    local zip
    zip="$(mktemp -d)/ZXV-notarize.zip"
    ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
    notarize "$zip" "$app"
    rm -rf "$(dirname "$zip")"
    if notary_args >/dev/null; then
        spctl --assess --type execute --verbose=2 "$app"
    fi
}

mac_dmg() {
    local dmg="$1"
    [ -f "$dmg" ] || { echo "no such disk image: $dmg" >&2; exit 2; }
    mac_preflight || return 0
    if ! identity_checked; then
        log "disk image left unsigned (no ZXV_SIGN_IDENTITY)"
        return 0
    fi
    local kc=()
    [ -n "${ZXV_KEYCHAIN:-}" ] && kc=(--keychain "$ZXV_KEYCHAIN")
    codesign --force --timestamp ${kc[@]+"${kc[@]}"} --sign "$ZXV_SIGN_IDENTITY" "$dmg"
    log "disk image signed"
    notarize "$dmg" "$dmg"
}

case "${1:-}" in
    --mac-app) [ $# -eq 2 ] || { echo "usage: $0 --mac-app ZXV.app" >&2; exit 2; }
               mac_app "$2"; exit 0 ;;
    --mac-dmg) [ $# -eq 2 ] || { echo "usage: $0 --mac-dmg FILE.dmg" >&2; exit 2; }
               mac_dmg "$2"; exit 0 ;;
    "") ;;
    *) echo "usage: $0 [--mac-app ZXV.app | --mac-dmg FILE.dmg]" >&2; exit 2 ;;
esac

# ============================================== the original ZSP signing job
cd "$here"

KEYS=build_system/keys
mkdir -p "$KEYS"
if [ ! -f "$KEYS/root_priv.pem" ]; then
    echo "[keys] generating root Ed25519 keypair (first run)"
    openssl genpkey -algorithm ed25519 -out "$KEYS/root_priv.pem" 2>/dev/null
fi
openssl pkey -in "$KEYS/root_priv.pem" -pubout -outform DER 2>/dev/null \
    | tail -c 32 > "$KEYS/root_pub.bin"

# 1) build the ELF app
( cd kernel/userapp && ./build_hello.sh >/dev/null )

# 2) sign it -> hello.zsp + kernel/userapp/hello_signed.h
python3 build_system/sign_package.py \
    kernel/userapp/hello.elf \
    "$KEYS/root_priv.pem" "$KEYS/root_pub.bin" \
    kernel/userapp/hello.zsp \
    kernel/userapp/hello_signed.h \
    hello

echo "signed app ready: kernel/userapp/hello.zsp + hello_signed.h"
