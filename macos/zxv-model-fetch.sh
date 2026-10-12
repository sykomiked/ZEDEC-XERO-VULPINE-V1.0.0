#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# zxv-model-fetch.sh MANIFEST MODELS_DIR — fetch the first model in the
# manifest that downloads and verifies, and install it.
#
# Sources are tried in order. When the line has a CID, IPFS comes first: a
# local IPFS node (works offline and on a LAN when a node there holds the
# file), then public gateways. Override with
# ZXV_IPFS_GATEWAYS="http://my-node:8080 https://gw.example". Then any HTTPS
# mirrors listed after the licence (for example the model's Hugging Face
# file), so a model with no IPFS copy yet can still be fetched. The file
# is downloaded to a hidden .part file and installed only after its exact
# size and sha256 match the manifest, which is inside the signed app, so a
# hostile gateway or mirror can waste time but cannot install anything.
# A model already present with the right hash is kept and nothing is fetched.
#
# Exit 0 when a verified model is installed (or already there), 1 if none
# could be, 2 on bad usage. Uses only curl and shasum/sha256sum.
set -euf   # -f: no globbing of manifest fields

[ $# -eq 2 ] || { echo "usage: $0 MANIFEST MODELS_DIR" >&2; exit 2; }
manifest=$1
dest=$2
gateways=${ZXV_IPFS_GATEWAYS:-"http://127.0.0.1:8080 https://ipfs.io https://dweb.link"}

if command -v shasum >/dev/null 2>&1; then sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
elif command -v sha256sum >/dev/null 2>&1; then sha() { sha256sum "$1" | cut -d' ' -f1; }
else echo "no shasum or sha256sum" >&2; exit 1
fi
size() { wc -c < "$1" | tr -d ' '; }

mkdir -p "$dest"
umask 077
log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"; }

# Each field is checked against a strict pattern before it is used in a
# path or a URL, so a malformed manifest line cannot escape MODELS_DIR or
# change the request.
valid() {
    printf '%s' "$1" | grep -Eq "$2"
}

status=1
while read -r name hash bytes cid licence rest; do
    case "$name" in ''|'#'*) continue ;; esac
    if ! valid "$name" '^[A-Za-z0-9][A-Za-z0-9._-]{0,120}\.gguf$' ||
       ! valid "$hash" '^[0-9a-f]{64}$' ||
       ! valid "$bytes" '^[0-9]{1,13}$' ||
       ! valid "$cid" '^(-|baf[a-z2-7]{20,120}|Qm[1-9A-HJ-NP-Za-km-z]{44})$' ||
       [ -z "${licence:-}" ]; then
        log "skipping a malformed manifest line ($name)"
        continue
    fi
    final="$dest/$name"
    if [ -f "$final" ] && [ "$(size "$final")" = "$bytes" ] && [ "$(sha "$final")" = "$hash" ]; then
        log "$name is already installed and verified"
        status=0
        break
    fi
    part="$dest/.$name.part"
    log "fetching $name ($bytes bytes, licence $licence, CID $cid)"
    # One source per line: curl's allowed protocols, then the URL.
    sources=""
    if [ "$cid" != - ]; then
        for gw in $gateways; do
            valid "$gw" '^https?://[][A-Za-z0-9.:-]+$' || { log "skipping bad gateway $gw"; continue; }
            sources="$sources
=http,https $gw/ipfs/$cid?filename=$name"
        done
    fi
    for url in ${rest:-}; do
        # HTTPS only (plain HTTP just for a mirror on this machine), and
        # redirects may not drop to HTTP.
        if valid "$url" '^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?/[A-Za-z0-9._~/%+=-]*$'; then
            sources="$sources
=https $url"
        elif valid "$url" '^http://(127\.0\.0\.1|localhost)(:[0-9]{1,5})?/[A-Za-z0-9._~/%+=-]*$'; then
            sources="$sources
=http,https $url"
        else
            log "skipping bad mirror $url"
        fi
    done
    while read -r proto src; do
        [ -n "$src" ] || continue
        rm -f "$part"
        log "  trying ${src%%\?*}"
        if curl -fsSL --proto "$proto" --proto-redir "$proto" \
                --connect-timeout 10 --retry 2 --max-filesize "$bytes" \
                -o "$part" "$src"; then
            got_size=$(size "$part")
            got_hash=$(sha "$part")
            if [ "$got_size" = "$bytes" ] && [ "$got_hash" = "$hash" ]; then
                chmod 0644 "$part"
                mv -f "$part" "$final"
                log "installed $final (sha256 verified)"
                status=0
                break
            fi
            log "  that source sent a file that does not match the manifest; discarded"
        fi
    done <<EOF_SOURCES
$sources
EOF_SOURCES
    rm -f "$part"
    [ "$status" = 0 ] && break
done < "$manifest"

[ "$status" = 0 ] || log "no model could be fetched and verified"
exit "$status"
