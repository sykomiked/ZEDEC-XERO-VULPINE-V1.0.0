# ZXV Release Signing — offline root, digest-only ceremony

The root key is the whole trust chain: the kernel refuses code it did not sign
(ZSP), the A/B updater refuses images it did not sign, and the installer refuses
bundles it did not sign. This document is the operator runbook.

## The one rule

**The root private key never touches a networked machine. Ever.**

Not the build server, not a CI runner, not a laptop with Wi-Fi on. A root key
that has existed on a shared or rented host must be treated as compromised from
birth — anyone who has had root there, or a copy of the disk image, can sign
releases that every ZXV install on earth accepts. Recovering from that means
re-keying every deployed system.

This is why the ceremony script refuses to run when it sees a default route.

## Why this design is practical (the digest-only trick)

An offline root is usually painful: you have to carry hundreds of megabytes of
artifacts to the air-gapped box. We don't. The build host emits a small text
**request** listing every artifact's SHA-256; you carry only that. Signing the
digest list is equivalent to signing the artifacts (SHA-256 is
collision-resistant), so:

- artifacts never leave the build host,
- the private key never leaves the air-gapped machine,
- what crosses the gap is a few hundred bytes each way, on a USB stick.

## One-time: mint the root key

On an air-gapped machine (network physically off, or a live USB):

```
bash build_system/keyceremony_root.sh /media/usb/zxv-root
```

You will be asked for a passphrase — long, unique, and yours alone. Outputs:

| file | secrecy | where it lives |
|---|---|---|
| `root_priv.pem` | **SECRET** (AES-256 encrypted) | encrypted removable media / safe. Back it up — losing it means never being able to sign for existing installs. |
| `root_pub.pem` | public | commit to `PROVENANCE/` |
| `root_pub.bin` | public | raw 32-byte key, the kernel's trust anchor |
| `ROOT_TRUST_ANCHOR.txt` | public | commit to `PROVENANCE/`, and **publish the key-id widely** |

Then activate pinning:

```
cp /media/usb/zxv-root/ROOT_TRUST_ANCHOR.txt PROVENANCE/
cp /media/usb/zxv-root/root_pub.pem          PROVENANCE/
```

### Publish the key-id

Put the key-id on the website, in the README, in release notes — anywhere an
attacker who compromises the download server cannot also edit. Verification is
only meaningful against an anchor the attacker does not control. **A public key
that shipped inside the artifact being verified proves nothing**: whoever
replaced the artifact replaced the key too and re-signed. That specific attack
is covered by a regression test (`test_release_signing.sh` case 4).

## Every release

**1. Build host** — no key required:

```
bash build_system/release_request.sh 1.0.0 dist/
```

Produces `zxv-release-1.0.0.request`. Copy it to a USB stick.

**2. Air-gapped machine** — read what you are signing, then sign:

```
bash build_system/sign_release_offline.sh zxv-release-1.0.0.request /media/usb/zxv-root/root_priv.pem
```

It prints the full request and requires you to type `sign`. **Actually read it.**
A confirmation you always say yes to buys nothing over an online key. Carry back
only the 64-byte `.sig`.

**3. Anywhere** — verify before publishing, and tell users to verify too:

```
bash build_system/verify_release.sh zxv-release-1.0.0.request dist/
```

Checks, fail-closed and in order: the anchor's key-id matches the pinned anchor;
the Ed25519 signature verifies; every artifact matches the exact signed size and
digest; nothing is missing.

## What is tested

`make verify-all` runs `build_system/test_release_signing.sh`, which is almost
entirely negative cases — a verifier only tested on the happy path is
indistinguishable from one that always returns OK:

1. a valid signed release verifies
2. a tampered artifact is refused
3. a tampered request is refused
4. a valid signature by a **different** root key is refused (substitution)
5. an unsigned release is refused
6. a missing artifact is refused
7. an unencrypted (passphrase-less) key is refused as a signing key

It also runs `install/install_zxv.py --selftest`, which covers path-traversal
confinement, manifest authentication, and root-key pinning.

## The in-tree development key

`build_system/keys/root_priv.pem` is a **development-only** key used by
`build_signed_app.sh` for local app-signing experiments. It is gitignored and
has never been committed (verified against the full history). It is
unencrypted, therefore `sign_release_offline.sh` refuses it outright, and once a
real anchor is pinned, anything it signed fails verification. It must never sign
a release.
