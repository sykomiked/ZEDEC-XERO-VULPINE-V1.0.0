<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Secrets and post-quantum crypto

**Who this is for:** developers who sign messages, agree on shared keys, or
hold secret keys in memory.
**What you will be able to do:** sign and verify with ML-DSA-87, agree a key
with ML-KEM-1024, and keep secret keys wiped and out of logs.

## The algorithms

| Name | What it does | Capability |
|---|---|---|
| ML-DSA-87 (FIPS 204) | post-quantum signatures | `CRYPTO_SIGN` |
| Ed25519 | classic signatures, small and fast | `CRYPTO_SIGN` |
| ML-KEM-1024 (FIPS 203) | post-quantum key agreement ("key encapsulation") | `CRYPTO_KEM` |
| SHA3-256, SHAKE256, Keccak-256 | hashes (`zxv.Hashes`) | none |

**Post-quantum** means designed to resist attacks by a future large quantum
computer. FIPS 203 and FIPS 204 are the US standards that define ML-KEM and
ML-DSA.

## Signing

```python
import zxv
from zxv import Cap

with zxv.Context() as ctx:
    root = ctx.root_capability()
    dsa = zxv.MlDsa87(root.attenuate(Cap.CRYPTO_SIGN))
    with dsa.generate() as keys:
        msg = b"pay 150.00 NGN to ada"
        sig = dsa.sign(keys.secret_key, msg, context=b"zxv-agent")
        len(keys.public_key), len(sig)                                  # (2592, 4627)
        dsa.verify(keys.public_key, msg, sig, context=b"zxv-agent")     # True
        dsa.verify(keys.public_key, b"pay 1500.00 NGN to ada", sig,
                   context=b"zxv-agent")                                # False
    keys.secret_key.closed                                              # True: wiped
```

The `context` bytes bind a signature to one purpose, so a signature made for
one part of your system cannot be replayed in another. A failed check
returns `False`; it does not raise.

## Agreeing a shared key

ML-KEM lets two sides end up with the same 32-byte secret. The receiver
publishes a public key. The sender **encapsulates** against it and gets a
ciphertext plus the secret. The receiver **decapsulates** the ciphertext with
its secret key and gets the same secret.

```python
kem = zxv.MlKem1024(root.attenuate(Cap.CRYPTO_KEM))
with kem.generate() as alice:                                   # receiver
    with kem.encapsulate(alice.public_key) as enc:              # sender
        len(enc.ciphertext)                                     # 1568
        with kem.decapsulate(alice.secret_key, enc.ciphertext) as shared:
            shared.equals(enc.shared_secret), len(shared)       # (True, 32)
```

Use the shared secret as a key for a symmetric cipher of your choice (ZXV
does not include one), then drop it.

## Keeping secrets secret

Secret keys and shared secrets are `SecretKey` objects, not `bytes`:

- They live in memory the SDK allocates outside the garbage-collected heap
  where the language allows.
- `close()` (or leaving the `with` block) overwrites them with zeros using
  `zxv_secure_zero`, a wipe the compiler cannot optimise away.
- `repr()` shows only the kind and size: `<zxv.SecretKey ml-dsa-87 4896 bytes>`.
- They refuse to be pickled (`TypeError`).
- `equals` compares in constant time.
- To get raw bytes you must call `export_bytes_unsafe()`. Once you do, the
  bytes are an ordinary object the SDK cannot wipe.

In C, wipe every secret buffer yourself:

```c
static uint8_t ek[ZXV_MLKEM1024_EK_BYTES], dk[ZXV_MLKEM1024_DK_BYTES], ct[ZXV_MLKEM1024_CT_BYTES];
uint8_t ss_sender[ZXV_MLKEM_SS_BYTES], ss_receiver[ZXV_MLKEM_SS_BYTES];
zxv_mlkem1024_keygen(kem, ek, dk);                   /* receiver */
zxv_mlkem1024_encaps(kem, ek, ct, ss_sender);        /* sender   */
zxv_mlkem1024_decaps(kem, dk, ct, ss_receiver);      /* receiver */
/* ... use the secret ... */
zxv_secure_zero(dk, sizeof dk);
zxv_secure_zero(ss_sender, sizeof ss_sender);
zxv_secure_zero(ss_receiver, sizeof ss_receiver);
```

## Honest limits

- **Not FIPS 140-3 validated** and not independently audited. The algorithms
  follow FIPS 203 and 204 and pass the NIST test vectors; that is not the
  same as a validated module.
- **Not measured for side channels.** The code is the reference
  implementation. Whether it runs in constant time on your hardware has not
  been measured.
- **Wiping has limits.** A copy you made yourself, swap space, hibernation
  files and core dumps can still hold secrets.

Read the full list in [the security model](../SECURITY.md) and
[SDK_DESIGN §7](../SDK_DESIGN.md#7-secrets).

## Next

- [Wire frames](wire-frames.md)
- [Conformance testing](conformance-testing.md): the crypto vectors every SDK
  checks.
