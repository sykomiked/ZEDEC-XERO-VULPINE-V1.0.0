#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
"""zxv_publish_update.py - make and sign a ZXV update manifest for a bucket.

A bucket is a directory that will be added to IPFS (`ipfs add -r`). This tool
writes two files into it:

    zxv-update.manifest      the signed list of update files (format below)
    zxv-update.manifest.sig  ML-DSA-65 (FIPS 204) signature, 3309 bytes

The kernel side (kernel/src/update/zx_upcheck.c) installs nothing from a
bucket unless this signature verifies under a release key the user trusts for
that bucket. Every CID this tool writes is the CID Kubo gives the same file
with `ipfs add --cid-version=1` (256 KiB chunks, raw leaves, balanced DAG,
174 links per node).

THE RELEASE KEY IS SECRET. Never commit a key file. Keep it offline. Pass its
path on the command line; this tool never looks for a key anywhere else.

Commands
    keygen  --out KEYFILE [--pub PUBFILE]   new key (seed file, mode 0600)
    pubkey  --key KEYFILE [--out PUBFILE] [--c-array NAME]
    sign    --key KEYFILE --dir DIR --release X.Y.Z --min-version X.Y.Z
            [--issued UNIX_SECONDS] (--entry KIND:ARCH:PATH[:X.Y.Z] ... | --all KIND:ARCH)
    verify  --pub PUBFILE --dir DIR
    cid     FILE...                          print Kubo-identical CIDv1s

Manifest format (strict, ASCII, LF only, single spaces, nothing else):
    zxv-update-manifest 1
    release X.Y.Z
    min-version X.Y.Z
    issued UNIX_SECONDS
    entry KIND ARCH X.Y.Z SIZE CID SHA256HEX PATH      (1..32 lines, sorted by PATH)
    end
X, Y, Z are 0..999 without leading zeros. KIND is one of kernel app module
firmware data doc. ARCH is one of x86_64 aarch64 riscv64 riscv32 arm32 x86
any. SIZE is the file length. CID is CIDv1 base32 (raw or dag-pb). SHA256HEX
is the SHA-256 of the whole file. PATH is relative to the bucket root, made
of [A-Za-z0-9._+-] components joined by '/', at most 200 bytes. The
signature is ML-DSA-65 with context "zxv-update-manifest-v1" over the exact
bytes of the manifest file.

ML-DSA comes from the vendored pq-crystals reference code in
kernel/src/pqsec/mldsa (checked there against NIST ACVP), compiled on the fly
with $CC (default: cc) into a temporary shared library.
"""

import argparse
import ctypes
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

MANIFEST = "zxv-update.manifest"
SIGFILE = "zxv-update.manifest.sig"
SIG_CTX = b"zxv-update-manifest-v1"
PK_BYTES, SK_BYTES, SIG_BYTES = 1952, 4032, 3309
KEY_MAGIC = "zxv-mldsa65-seed-v1"
KINDS = ("kernel", "app", "module", "firmware", "data", "doc")
ARCHES = ("x86_64", "aarch64", "riscv64", "riscv32", "arm32", "x86", "any")
MAX_ENTRIES = 32
MAX_MANIFEST = 16384
MAX_PATH = 200

HERE = os.path.dirname(os.path.abspath(__file__))
MLDSA_DIR = os.path.join(HERE, "..", "kernel", "src", "pqsec", "mldsa")

GLUE = r"""
#include <stdint.h>
#include <stddef.h>
#include "params.h"
#include "sign.h"
#include "randombytes.h"
static uint8_t g_seed[SEEDBYTES];
void randombytes(uint8_t *out, size_t n) { for (size_t i = 0; i < n; i++) out[i] = g_seed[i % SEEDBYTES]; }
int zxv_keygen(const uint8_t *seed, uint8_t *pk, uint8_t *sk) {
    for (int i = 0; i < SEEDBYTES; i++) g_seed[i] = seed[i];
    int r = crypto_sign_keypair(pk, sk);
    for (int i = 0; i < SEEDBYTES; i++) g_seed[i] = 0;
    return r;
}
int zxv_sign(const uint8_t *sk, const uint8_t *m, size_t mlen, const uint8_t *ctx, size_t clen,
             const uint8_t *rnd, uint8_t *sig) {
    uint8_t pre[257]; size_t siglen = 0;
    if (clen > 255) return -1;
    pre[0] = 0; pre[1] = (uint8_t) clen;
    for (size_t i = 0; i < clen; i++) pre[2 + i] = ctx[i];
    return crypto_sign_signature_internal(sig, &siglen, m, mlen, pre, 2 + clen, rnd, sk);
}
int zxv_verify(const uint8_t *pk, const uint8_t *m, size_t mlen, const uint8_t *ctx, size_t clen,
               const uint8_t *sig) {
    return crypto_sign_verify(sig, CRYPTO_BYTES, m, mlen, ctx, clen, pk);
}
"""


def die(msg):
    sys.stderr.write("zxv_publish_update: " + msg + "\n")
    sys.exit(1)


# ---- ML-DSA-65 via the vendored reference code -------------------------------

_lib = None
_tmp = None


def mldsa():
    global _lib, _tmp
    if _lib is not None:
        return _lib
    src = os.path.abspath(MLDSA_DIR)
    if not os.path.isfile(os.path.join(src, "sign.c")):
        die("cannot find the vendored ML-DSA code at " + src)
    _tmp = tempfile.mkdtemp(prefix="zxv-mldsa-")
    glue = os.path.join(_tmp, "glue.c")
    with open(glue, "w") as f:
        f.write(GLUE)
    so = os.path.join(_tmp, "libzxvmldsa.so")
    cfiles = sorted(os.path.join(src, n) for n in os.listdir(src) if n.endswith(".c"))
    cc = os.environ.get("CC", "cc")
    cmd = [cc, "-O2", "-shared", "-fPIC", "-I", src, "-o", so, glue] + cfiles
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as e:
        die("could not build ML-DSA helper with %s: %s" % (cc, getattr(e, "stderr", e)))
    _lib = ctypes.CDLL(so)
    for fn in (_lib.zxv_keygen, _lib.zxv_sign, _lib.zxv_verify):
        fn.restype = ctypes.c_int
    return _lib


def _cleanup():
    if _tmp:
        shutil.rmtree(_tmp, ignore_errors=True)


def keypair(seed):
    pk = ctypes.create_string_buffer(PK_BYTES)
    sk = ctypes.create_string_buffer(SK_BYTES)
    if mldsa().zxv_keygen(seed, pk, sk) != 0:
        die("keygen failed")
    return pk.raw, sk


def sign(sk, msg):
    sig = ctypes.create_string_buffer(SIG_BYTES)
    rnd = os.urandom(32)  # hedged signing (FIPS 204)
    if mldsa().zxv_sign(sk, msg, ctypes.c_size_t(len(msg)), SIG_CTX,
                        ctypes.c_size_t(len(SIG_CTX)), rnd, sig) != 0:
        die("sign failed")
    return sig.raw


def verify(pk, msg, sig):
    if len(pk) != PK_BYTES or len(sig) != SIG_BYTES:
        return False
    return mldsa().zxv_verify(pk, msg, ctypes.c_size_t(len(msg)), SIG_CTX,
                              ctypes.c_size_t(len(SIG_CTX)), sig) == 0


def load_seed(path):
    try:
        with open(path, "r") as f:
            txt = f.read()
    except OSError as e:
        die("cannot read key file: %s" % e)
    m = re.fullmatch(KEY_MAGIC + r" ([0-9a-f]{64})\n", txt)
    if not m:
        die("not a %s key file: %s" % (KEY_MAGIC, path))
    return bytes.fromhex(m.group(1))


# ---- Kubo-identical UnixFS file CIDs ------------------------------------------

B32 = "abcdefghijklmnopqrstuvwxyz234567"
CHUNK, LINKS = 262144, 174


def varint(v):
    out = bytearray()
    while v >= 0x80:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    out.append(v)
    return bytes(out)


def cid_bin(codec, digest):
    return b"\x01" + varint(codec) + b"\x12\x20" + digest


def cid_str(cb):
    bits, acc, out = 0, 0, []
    for b in cb:
        acc = (acc << 8) | b
        bits += 8
        while bits >= 5:
            out.append(B32[(acc >> (bits - 5)) & 31])
            bits -= 5
    if bits:
        out.append(B32[(acc << (5 - bits)) & 31])
    return "b" + "".join(out)


def _node(links):
    """links: [(cid_bin, tsize, fsize)] -> (cid_bin, tsize, fsize) of a File node."""
    body = bytearray()
    fsize = sum(l[2] for l in links)
    tsum = sum(l[1] for l in links)
    for cb, ts, _ in links:
        link = b"\x0a" + varint(len(cb)) + cb + b"\x12\x00" + b"\x18" + varint(ts)
        body += b"\x12" + varint(len(link)) + link
    data = b"\x08\x02" + b"\x18" + varint(fsize)
    for l in links:
        data += b"\x20" + varint(l[2])
    body += b"\x0a" + varint(len(data)) + data
    cb = cid_bin(0x70, hashlib.sha256(bytes(body)).digest())
    return (cb, tsum + len(body), fsize)


def file_cid(path):
    """(cid string, size, sha256 hex) the way `ipfs add --cid-version=1` builds it."""
    levels = [[]]
    total, sh = 0, hashlib.sha256()

    def add(lvl, link):
        if lvl == len(levels):
            levels.append([])
        if len(levels[lvl]) == LINKS:
            up = _node(levels[lvl])
            levels[lvl] = []
            add(lvl + 1, up)
        levels[lvl].append(link)

    leaves = 0
    with open(path, "rb") as f:
        while True:
            chunk = f.read(CHUNK)
            if not chunk and leaves:
                break
            total += len(chunk)
            sh.update(chunk)
            add(0, (cid_bin(0x55, hashlib.sha256(chunk).digest()), len(chunk), len(chunk)))
            leaves += 1
            if len(chunk) < CHUNK:
                break
    if len(levels) == 1 and len(levels[0]) == 1:
        root = levels[0][0]
    else:
        top = len(levels) - 1
        lvl = 0
        while lvl < top:
            if levels[lvl]:
                up = _node(levels[lvl])
                levels[lvl] = []
                add(lvl + 1, up)
                top = len(levels) - 1
            lvl += 1
        root = _node(levels[top])
    return cid_str(root[0]), total, sh.hexdigest()


# ---- manifest -------------------------------------------------------------------

VER_RE = re.compile(r"(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})")
COMP_RE = re.compile(r"[A-Za-z0-9._+-]+")


def check_version(v, what):
    if not VER_RE.fullmatch(v):
        die("%s must be X.Y.Z with 0..999 and no leading zeros: %r" % (what, v))
    return tuple(int(x) for x in v.split("."))


def check_path(p):
    if len(p.encode()) > MAX_PATH or not p:
        die("path too long or empty: %r" % p)
    for c in p.split("/"):
        if not COMP_RE.fullmatch(c) or c in (".", ".."):
            die("bad path component in %r (allowed: [A-Za-z0-9._+-])" % p)
    if p in (MANIFEST, SIGFILE):
        die("the manifest cannot list itself")
    return p


def build_manifest(release, minv, issued, entries):
    lines = ["zxv-update-manifest 1", "release " + release, "min-version " + minv,
             "issued %d" % issued]
    for e in sorted(entries, key=lambda e: e["path"].encode()):
        lines.append("entry %s %s %s %d %s %s %s" % (e["kind"], e["arch"], e["version"],
                                                     e["size"], e["cid"], e["sha256"], e["path"]))
    lines.append("end")
    text = ("\n".join(lines) + "\n").encode("ascii")
    if len(text) > MAX_MANIFEST:
        die("manifest over %d bytes" % MAX_MANIFEST)
    return text


def cmd_keygen(a):
    if os.path.exists(a.out):
        die("refusing to overwrite " + a.out)
    seed = os.urandom(32)
    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write("%s %s\n" % (KEY_MAGIC, seed.hex()))
    pk, _ = keypair(seed)
    if a.pub:
        with open(a.pub, "wb") as f:
            f.write(pk)
    print("key written to %s (mode 0600). KEEP IT SECRET; never commit it." % a.out)
    print("public key sha256: " + hashlib.sha256(pk).hexdigest())


def cmd_pubkey(a):
    pk, _ = keypair(load_seed(a.key))
    if a.out:
        with open(a.out, "wb") as f:
            f.write(pk)
    if a.c_array:
        print("static const uint8_t %s[%d] = {" % (a.c_array, len(pk)))
        for i in range(0, len(pk), 12):
            print("    " + ", ".join("0x%02x" % b for b in pk[i:i + 12]) + ",")
        print("};")
    print("public key sha256: " + hashlib.sha256(pk).hexdigest(), file=sys.stderr)


def cmd_sign(a):
    release = a.release
    rv = check_version(release, "--release")
    mv = check_version(a.min_version, "--min-version")
    if mv > rv:
        die("--min-version is above --release")
    issued = a.issued if a.issued is not None else int(time.time())
    if issued < 0 or issued > 2**63 - 1:
        die("--issued out of range")
    root = os.path.abspath(a.dir)
    if not os.path.isdir(root):
        die("not a directory: " + a.dir)
    specs = []
    for e in a.entry or []:
        parts = e.split(":")
        if len(parts) not in (3, 4):
            die("--entry is KIND:ARCH:PATH[:X.Y.Z]: %r" % e)
        ver = parts[3] if len(parts) == 4 else release
        specs.append((parts[0], parts[1], parts[2], ver))
    if a.all:
        kind, _, arch = a.all.partition(":")
        for dp, dns, fns in os.walk(root):
            dns.sort()
            for fn in sorted(fns):
                rel = os.path.relpath(os.path.join(dp, fn), root).replace(os.sep, "/")
                if rel in (MANIFEST, SIGFILE):
                    continue
                specs.append((kind, arch, rel, release))
    if not specs or len(specs) > MAX_ENTRIES:
        die("need 1..%d entries" % MAX_ENTRIES)
    entries, seen = [], set()
    for kind, arch, path, ver in specs:
        if kind not in KINDS:
            die("KIND must be one of " + " ".join(KINDS))
        if arch not in ARCHES:
            die("ARCH must be one of " + " ".join(ARCHES))
        check_path(path)
        if check_version(ver, "entry version") > rv:
            die("entry version above the release: " + path)
        if path in seen:
            die("duplicate path " + path)
        seen.add(path)
        full = os.path.join(root, *path.split("/"))
        if not os.path.isfile(full) or os.path.islink(full):
            die("not a regular file in the bucket: " + path)
        cid, size, sh = file_cid(full)
        if size > 0xFFFFFFFF:
            die("file over 4 GiB: " + path)
        entries.append(dict(kind=kind, arch=arch, version=ver, size=size, cid=cid, sha256=sh,
                            path=path))
    text = build_manifest(release, a.min_version, issued, entries)
    pk, sk = keypair(load_seed(a.key))
    sig = sign(sk, text)
    ctypes.memset(sk, 0, SK_BYTES)
    if not verify(pk, text, sig):
        die("self-check of the new signature failed")
    with open(os.path.join(root, MANIFEST), "wb") as f:
        f.write(text)
    with open(os.path.join(root, SIGFILE), "wb") as f:
        f.write(sig)
    sys.stdout.write(text.decode())
    print("signed by key sha256 %s" % hashlib.sha256(pk).hexdigest(), file=sys.stderr)


def cmd_verify(a):
    with open(a.pub, "rb") as f:
        pk = f.read()
    root = os.path.abspath(a.dir)
    with open(os.path.join(root, MANIFEST), "rb") as f:
        text = f.read()
    with open(os.path.join(root, SIGFILE), "rb") as f:
        sig = f.read()
    if not verify(pk, text, sig):
        die("signature does NOT verify under this key")
    bad = 0
    for line in text.decode("ascii").split("\n"):
        if line.startswith("entry "):
            f = line.split(" ")
            cid, size, sh = file_cid(os.path.join(root, *f[7].split("/")))
            if (cid, str(size), sh) != (f[5], f[4], f[6]):
                print("MISMATCH " + f[7])
                bad += 1
    if bad:
        die("%d entries do not match the files" % bad)
    print("OK: signature verifies and every entry matches its file")


def cmd_cid(a):
    for p in a.files:
        cid, size, sh = file_cid(p)
        print("%s %d %s %s" % (cid, size, sh, p))


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("--out", required=True)
    k.add_argument("--pub")
    k = sub.add_parser("pubkey")
    k.add_argument("--key", required=True)
    k.add_argument("--out")
    k.add_argument("--c-array")
    k = sub.add_parser("sign")
    k.add_argument("--key", required=True)
    k.add_argument("--dir", required=True)
    k.add_argument("--release", required=True)
    k.add_argument("--min-version", required=True)
    k.add_argument("--issued", type=int)
    k.add_argument("--entry", action="append")
    k.add_argument("--all")
    k = sub.add_parser("verify")
    k.add_argument("--pub", required=True)
    k.add_argument("--dir", required=True)
    k = sub.add_parser("cid")
    k.add_argument("files", nargs="+")
    a = p.parse_args()
    try:
        {"keygen": cmd_keygen, "pubkey": cmd_pubkey, "sign": cmd_sign, "verify": cmd_verify,
         "cid": cmd_cid}[a.cmd](a)
    finally:
        _cleanup()


if __name__ == "__main__":
    main()
