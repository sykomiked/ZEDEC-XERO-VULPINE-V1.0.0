#!/usr/bin/env bash
# golden_login_check.sh — the golden disc's whole path on arm64: boot, the
# boot model answers, log in with a passport seed at the console, ask the
# model a question, log out.
#
#   bash build_system/baremetal_images.sh arm64
#   bash build_system/golden_login_check.sh [seconds]
#
# Builds the same small golden disc as test_golden_disc.sh (the tiny real
# qwen2 GGUF from mk_boot_test_model.py), boots it under UEFI with QEMU's
# serial wired to a driver, waits for the EL0 shell ("P-TERM/EL0", "> ") and
# types, checking each answer:
#
#   ask hello                  [AI_REFUSED]  nobody is logged in
#   login <seed, one typo>     [LOGIN_FAIL] seed checksum wrong
#   login <seed>               [LOGIN_OK] <did>
#   whoami                     [WHOAMI] <did>
#   ask <the boot question>    [AI_ANSWER] ... (100 prompt + 16 tokens, digest
#                              <the digest test_swarm_bootmodel pins>)
#   logout                     [LOGOUT] <did>
#   ask hello                  [AI_REFUSED]
#   create                     [NEW_SEED] <a fresh seed text>   (first time)
#   confirm <it, one typo>     [CREATE_FAIL]
#   confirm <it>               [CREATED] <new did>, [LOGIN_OK] <new did>
#   ask <the boot question>    [AI_ANSWER] ... the same digest
#   logout                     [LOGOUT] <new did>
#
# create needs the CPU's RNDR, so QEMU runs -cpu max.
# The seed and its DID come from the host's test_pp_console (make -C kernel
# test-pp-console), so the disc must build the same identity the host does.
# The seed text must never appear in the serial log: the shell echoes it as
# '*' and the kernel never prints it; the new seed is shown once, by create. Runs in build_system/Dockerfile.baremetal.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH=arm64
SECS="${1:-300}"
T="dist/discs/.golden-login-$ARCH"
NAME="zxv-$ARCH-golden-login"
IMG="dist/discs/$NAME.img"
MODEL=zxv-boot-test.gguf
LOG="golden_login_${ARCH}.log"
rm -rf "$T"; mkdir -p "$T/cache"

make -s -C kernel test-pp-console ZXV_TMP="$PWD/$T" > /dev/null
seed=$(sed -n 's/^  seed text \(.*\)$/\1/p' "$T/test_pp_console.log")
did=$(sed -n 's/^  did \(did:zxv:.*\)$/\1/p' "$T/test_pp_console.log")
[ -n "$seed" ] && [ -n "$did" ] || { echo "::error::test_pp_console printed no seed or DID"; exit 1; }
digest=$(sed -n 's/^#define TEST_MODEL_DIGEST *\(0x[0-9a-f]*\)u$/\1/p' kernel/src/swarm/test_swarm_bootmodel.c)
question=$(sed -n 's/^#define SWARM_BOOTMODEL_QUESTION *"\(.*\)"$/\1/p' kernel/src/swarm/swarm_bootmodel.h)
[ -n "$digest" ] && [ -n "$question" ] || { echo "::error::no pinned digest or boot question"; exit 1; }

python3 build_system/mk_boot_test_model.py "$T/cache/$MODEL"
sha=$(sha256sum "$T/cache/$MODEL" | cut -d' ' -f1)
bytes=$(stat -c %s "$T/cache/$MODEL")
echo "$MODEL $sha $bytes - Apache-2.0" > "$T/models"
printf 'LICENSE ZXV/LICENSES/LICENSE\n' > "$T/contents"
GOLDEN_MODELS="$T/models" GOLDEN_CONTENTS="$T/contents" GOLDEN_NAME="$NAME" \
  ZXV_MODEL_CACHE="$T/cache" GOLDEN_OFFLINE=1 bash build_system/mk_golden_disc.sh "$ARCH"

FW=
for f in "${AAVMF:-}" /usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd; do
  [ -n "$f" ] && [ -f "$f" ] && { FW=$f; break; }
done
[ -n "$FW" ] || { echo "::error::no arm64 UEFI firmware (install qemu-efi-aarch64)"; exit 1; }

python3 - "$SECS" "$LOG" "$seed" "$did" "$digest" "$question" -- \
  qemu-system-aarch64 -M virt,gic-version=3 -cpu max -m 512M -bios "$FW" \
  -drive "if=none,id=disc,format=raw,snapshot=on,file=$IMG" \
  -device virtio-blk-pci,drive=disc,bootindex=0 \
  -net none -display none -serial stdio -monitor none <<'EOF'
import os, subprocess, sys, time

secs, log, seed, did, digest, question = sys.argv[1:7]
cmd = sys.argv[8:]
deadline = time.monotonic() + int(secs)
p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
os.set_blocking(p.stdout.fileno(), False)
buf = bytearray()

def pump():
    try:
        d = p.stdout.read()
    except BlockingIOError:
        d = None
    if d:
        buf.extend(d)
    elif p.poll() is not None:
        return False
    else:
        time.sleep(0.05)
    return True

def wait_for(text, start):
    t = text.encode()
    while time.monotonic() < deadline:
        i = buf.find(t, start)
        if i >= 0:
            return i + len(t)
        if not pump():
            break
    return -1

def fail(why):
    open(log, "wb").write(bytes(buf))
    p.kill()
    sys.stdout.buffer.write(bytes(buf[-4000:]))
    print(f"\n::error::arm64 golden login: {why}")
    sys.exit(1)

def prompt(start):
    # The shell's prompt after start (kernel lines may follow it).
    if wait_for("\n> ", start) < 0:
        fail("the shell never came back to its prompt")

def say(line, expect):
    prompt(pos[0])
    start = len(buf)
    for ch in line.encode() + b"\r":
        p.stdin.write(bytes([ch]))
        p.stdin.flush()
        time.sleep(0.01)
    end = wait_for(expect, start)
    if end < 0:
        fail(f"typed a {line.split()[0]!r} line and never saw {expect!r}")
    pos[0] = end

pos = [0]
for m in ("[AI_OK] zxv-boot-test.gguf answered", "[BOOT_OK]", "P-TERM/EL0"):
    if wait_for(m, 0) < 0:
        fail(f"the boot never printed {m}")
pos[0] = wait_for("P-TERM/EL0", 0)

typo = list(seed)
typo[3] = "8" if typo[3] == "7" else "7"
say("ask hello", "[AI_REFUSED] log in first")
say("login " + "".join(typo), "[LOGIN_FAIL] seed checksum wrong")
say("login " + seed, f"[LOGIN_OK] {did}")
say("whoami", f"[WHOAMI] {did}")
say("ask " + question, f"tokens, digest {digest})")
ans = buf.rfind(b"[AI_ANSWER] ")
if ans < 0 or b"(100 prompt + 16 tokens, digest " + digest.encode() + b")" not in buf[ans:]:
    fail("the answer is not the boot answer the host pins")
say("logout", f"[LOGOUT] {did}")
say("ask hello", "[AI_REFUSED] log in first")

# First time: make a passport from nothing, then type it back.
say("create", "confirm <seed text>")
i = buf.rfind(b"[NEW_SEED] ")
new = bytes(buf[i + 11:buf.index(b"\n", i)]).strip().decode() if i >= 0 else ""
if len(new) != 65 or new == seed:
    fail(f"create did not show a fresh seed text ({new!r})")
bad = list(new)
bad[0] = "8" if bad[0] == "7" else "7"
say("confirm " + "".join(bad), "[CREATE_FAIL]")
say("whoami", "[WHOAMI] nobody")
say("confirm " + new, "[LOGIN_OK] did:zxv:")
c = buf.rfind(b"[CREATED] did:zxv:")
if c < 0:
    fail("confirm logged in without [CREATED]")
new_did = bytes(buf[c + 10:buf.index(b"\n", c)]).strip().decode()
if new_did == did:
    fail("the new passport has the test seed's DID")
say("ask " + question, f"tokens, digest {digest})")
say("logout", f"[LOGOUT] {new_did}")
p.kill()
p.wait()
open(log, "wb").write(bytes(buf))
for secret in (seed, seed.replace("-", ""), "".join(typo), "".join(bad)):
    if secret.encode() in buf:
        fail("a typed seed text reached the serial log")
if buf.count(new.encode()) != 1:
    fail("the new seed was shown more than once (the confirm line echoed it)")
for bad in (b"[FAULT]", b"[EL0 FAULT]", b"PANIC"):
    if bad in buf:
        fail(f"the log has {bad.decode()}")
print(f"[golden-login] arm64: refused before login, typo refused, {did} logged in, "
      f"asked and answered (digest {digest}), logged out and refused again; first time: "
      f"created {new_did}, wrong confirmation refused, confirmed, asked and answered; "
      f"no typed seed echoed")
EOF

rm -rf "$T" "$IMG" "dist/discs/$NAME.sizes"
