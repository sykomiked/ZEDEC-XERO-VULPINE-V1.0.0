<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Disk encryption

Before this change, everything on the persistent disk was stored in the
clear. Anyone holding the disk could read every file.

Now an ARM64 kernel can encrypt the whole ZXVFS disk. It asks for a
passphrase at boot, and without it the disk is unreadable.

## Using it

| Command | Does |
|---|---|
| `encrypt` | **erases every file on the disk** and starts an empty encrypted one. It asks you to type `ERASE`, then a passphrase twice. It then prints a recovery key |
| `diskcrypt` | shows whether the disk is encrypted, and how many key slots are in use |

At every boot after that, the console asks for the passphrase or the recovery
key before the disk is mounted:

- After three wrong tries, the disk stays locked for that boot.
- An empty line leaves it locked straight away.
- A locked boot carries on without persistence.
- Crash reports and safe mode still work on a locked boot (see
  [CRASH_RECOVERY.md](CRASH_RECOVERY.md)).

Keep the recovery key somewhere away from the machine. It opens the disk if
the passphrase is lost, and nothing else can.

`encrypt` refuses to run when:

- the CPU has no hardware random source (FEAT_RNG, the `RNDR` instruction);
- the disk is too small;
- the disk is already encrypted.

## How it works

The code lives in `kernel/src/diskcrypt`.

### The cipher: Adiantum

Each 512-byte sector is encrypted with **Adiantum** ([Crowley and Biggers,
2018](https://eprint.iacr.org/2018/720)), using XChaCha12 and AES-256. The
sector number is the tweak.

Adiantum was designed for CPUs without AES instructions, which is the
situation a portable kernel is in. It has two properties that matter here:

- **No secret-dependent table lookups.** The usual disk cipher, AES-XTS, runs
  AES many times per sector. ZXV's existing AES is table-driven, and on a
  shared CPU cache that leaks key bits. Adiantum runs AES once per sector.
  That one call computes the S-box arithmetically (an inverse in GF(2^8)), so
  nothing in the cipher reads memory at an address that depends on a secret.
  The rest is ChaCha, NH and Poly1305, which only add, rotate, xor and
  multiply.
- **Wide-block.** Changing any bit of a sector changes the whole encrypted
  sector. XTS changes only one 16-byte block.

The parameters are the same as Linux's `adiantum(xchacha12,aes)` with a
32-byte tweak, and the sector number is used as Linux's `plain64` IV. So the
published test vectors apply unchanged. Our code is checked against all of
them:

- 7 XChaCha12 vectors;
- 6 NHPoly1305 vectors;
- 6 Adiantum vectors, including full 512-byte and 4096-byte sectors.

### Key slots

The disk layout is:

| Sectors | Holds |
|---|---|
| 0-7 | header copy A |
| 8-15 | header copy B |
| 16 onwards | the encrypted ZXVFS region |
| after the volume | the crash area, in the clear |

Every data sector is encrypted under one random 32-byte volume key. That key
is never stored as it is. The header has eight **key slots**, and each slot
wraps the volume key with ChaCha20-Poly1305. The wrapping key is derived from
a secret with Argon2id:

- **Passphrase slot.** Argon2id with at least 8 MiB of memory. The kernel
  uses 8 MiB and 3 passes.
- **Recovery slot.** A random 32-byte key. It is printed as eight groups of
  eight hex digits. Letter case, dashes and spaces are ignored when it is
  typed back.

Any one slot opens the disk. A passphrase is changed by adding a new slot and
removing the old one, so the data is never re-encrypted. The last slot can
not be removed.

Each slot's authentication covers:

- the header's fixed fields: uuid, size and cipher;
- the slot's own index;
- the slot's type;
- the slot's KDF settings.

So someone editing the disk can not move a slot, change its type or weaken
its Argon2id settings and still have it open. A passphrase slot whose
settings are below the floor is not even tried.

### Power loss

The header is kept twice. Each copy is sealed with SHA-256 and carries a
generation number. An update writes copy A, then copy B. Power lost partway
through leaves the other copy intact, so the disk always opens with either
the old key slots or the new ones.

### Erase

`diskcrypt_erase()` overwrites both header copies. That destroys the volume
key, and so every sector, in two writes rather than a pass over the disk. It
is the building block for remote wipe, which will only ever run with the
owner's explicit confirmation.

## What it does not do

- **It does not detect tampering with data.** Adiantum is length-preserving,
  like every cipher that fits a sector exactly, XTS included. A changed
  encrypted sector decrypts to garbage, not an error. The key slots *are*
  authenticated; the data sectors are not.
- **Someone who sees the disk at two times can tell which sectors changed.**
  The same data written to the same sector gives the same ciphertext.
- **The crash area is in the clear.** A crash record holds registers, the call
  chain and the last 2 KiB of console output. It sits outside the volume
  because the boot mode is chosen before anyone types a passphrase. Secrets
  typed at the console are not echoed, so they are not in it.
- **No in-place conversion.** `encrypt` starts an empty disk. Copy anything
  you need off it first.
- **ARM64 only.** The module builds freestanding for every target, but only
  the ARM64 kernel asks for a passphrase at boot.
- **8 MiB of Argon2id, not 64 MiB.** The kernel's work area is static memory,
  so it is kept small. A passphrase therefore needs to be a good one.
- **No hardware key yet.** A slot sealed by a hardware key (TPM or Secure
  Enclave, so no passphrase is needed on a trusted machine) is a later
  addition. The header has room for it.

## Tests

`make -C kernel test-diskcrypt` is part of `verify-all`. It runs these checks
(104 in all):

- every published XChaCha12, NHPoly1305 and Adiantum vector, and FIPS-197
  for the constant-time AES;
- wide-block behaviour: one flipped bit changes every 16-byte block of the
  sector;
- the header round trip, and a flipped byte anywhere is caught;
- format, unlock, lock, wrong keys, recovery keys, changing a passphrase, and
  removing the last slot is refused;
- tampering: a moved, re-typed or weakened slot, another uuid, or an
  oversized volume does not open;
- a header write cut after any number of sectors leaves a disk that opens;
- erase;
- nothing outside the volume is ever written.

The same tests also run under ASan and UBSan. The module is also built
freestanding for aarch64, i386, armv7a and rv32 with no libc and no compiler
helpers.

It was also checked end to end in QEMU (`virt`, `-cpu max` for `RNDR`) with
an 8 MiB disk, over six boots:

1. A file was written. Then `encrypt` ran, a second file was written, and
   the recovery key was printed.
2. The passphrase opened the disk. The second file was there and the first
   was gone. Neither file's text appears anywhere in the raw disk image.
3. A wrong passphrase was refused, then the recovery key, typed in lower
   case, opened the disk.
4. An empty line left the disk locked. The boot finished without
   persistence, and `diskcrypt` reported it locked.
5. The disk was unlocked, then `crashtest` faulted the kernel.
6. With the disk left locked, that boot still reported the crash and came up
   in safe mode.

`gen_diskcrypt_vectors.py` regenerates `test_diskcrypt_vectors.h` from the
published vectors.
