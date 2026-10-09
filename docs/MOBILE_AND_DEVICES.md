<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Phones, home computers and buying capacity

This guide explains how ZXV works on a phone. You can use the phone alone, or link it to a computer at home. When neither device is enough, you can buy capacity from other people. The last section lists what is not finished yet.

Where the code is:

| Part | Where |
|---|---|
| Device mesh (pairing, sessions, routing, settings sync, money confirmation) | `kernel/src/devmesh/` |
| Capacity market | `kernel/src/capmkt/` |
| Mobile C library and its build script | `mobile/core/` |
| Android app (Kotlin, Jetpack Compose, JNI) | `mobile/android/` |
| iOS app skeleton (SwiftUI) | `mobile/ios/` |

ZXV runs peer to peer. No step on this page needs a server run by us or by anyone else. It works on a phone with no network, on a home network, and over the internet.

## 1. Using only a phone

On first start, the app makes a device identity from a 64-byte random seed:

- On Android the seed is encrypted under a key in the Android Keystore.
- On iOS it is kept in the Keychain, on this device only.

The app then makes a "mesh of one": a signed list of your devices that holds just this phone.

What works on a phone alone:

- The assistant on a small local model, if one fits (see the table below). If none fits, the app says so and does not pretend.
- Settings, the wallet view and the device list.
- Talking to other people's devices over the local network or the internet.

The phone chooses a model by its memory. It gives a model at most half of its RAM, because the operating system and other apps need the rest.

| Model (Qwen2.5 Instruct, Q4_K_M, Apache-2.0) | File size | RAM needed | Runs on |
|---|---|---|---|
| 0.5B | 491 MB | about 700 MB | almost any phone from the last five years (2 GB RAM or more) |
| 1.5B | 1.1 GB | about 1.5 GB | phones with 4 GB RAM or more; the recommended phone model |
| 7B | 4.7 GB | about 5.4 GB | home computers with 8 GB RAM or more (phones only with 12 GB or more) |
| 14B | 9.0 GB | about 10.3 GB | home computers with 16 GB RAM or more |

A home computer may give a model three quarters of its RAM. Two models are left out on purpose. Qwen2.5 3B and 72B are under the Qwen licence, not Apache-2.0, so the catalogue does not include them.

How big the core library is (arm64, built with `-Os`, measured with `mobile/core/build_mobile_core.sh linux-arm64`):

| Measure | Size |
|---|---|
| Code linked into the app | about 152 KB |
| Data | 0.9 KB |
| Zero-initialised memory | about 519 KB (most of it is one mesh state) |
| Whole static archive | about 620 KB |
| Android JNI library with the core linked in, stripped | about 267 KB |

These sizes do not include the model files.

## 2. A phone and a home computer

A home computer usually has more memory, a GPU and mains power. When it is linked to your phone:

- It runs the large models.
- It holds your files.
- It answers the phone's prompts and streams the replies back.

The phone stays the device you carry. It is the one that approves payments.

### Pairing

Pairing is how a device joins your mesh.

1. On a device that is already in your mesh, open **Devices** and choose **Pair a device**. It shows a QR code and a 10-character code. Both stay valid for 10 minutes.
2. On the new device, scan the QR code, or type the code. The code is easier to read out over the phone. The QR code is stronger, because it carries a 32-byte secret and the inviter's key fingerprint.
3. The two devices run a post-quantum handshake. Then both screens show a 6-digit number. Check that the numbers match and tap **Yes** on both devices. If they don't match, tap **No**: someone may be in the middle, and pairing stops.
4. The inviting device signs a new device list that includes the new device and sends it across. Each device then knows the other's public key.

Three wrong codes cancel the invitation. A QR code that has been tampered with fails, because the key fingerprint it carries will not match.

### Roles and permissions

| Name | Meaning |
|---|---|
| home | a computer that stays at home: large models, files |
| thin | a phone or a laptop |
| standalone | a device that works alone |
| admin (flag) | may add, rename and remove devices |
| held (flag) | a device you carry; only held devices may approve payments |

To remove a lost device, open **Devices** and swipe or tap **Revoke**. Revoking is permanent. The removed device can never rejoin with the same key. If it comes online, it learns that it was removed and forgets your mesh.

The device list follows these rules:

- Its version number only goes up.
- Only an admin that is still in the list can sign a new one.
- A device's key can never be swapped.
- There must always be at least one admin.

### What runs where

For each assistant request, the phone decides where it runs:

1. **Home computer:** if it is reachable and can run the model. A large-model request goes there even on mobile data.
2. **This phone:** if the home computer is not reachable and a local model fits. If the request wanted a large model and only a small one fits, the app says the answer comes from a smaller model.
3. **Buy capacity:** if you are online and nothing of yours can run it.
4. **Unavailable:** if you are offline and nothing of yours can run it. The app says so.

If the home computer stops answering partway through a request (after 20 seconds), the phone falls back to its local model and tells you.

On mobile data, small requests stay on the phone. They go to the home computer only if the battery is low (under 20% and not charging), or if you allowed metered use in settings.

Settings sync between your devices. Each setting has a version clock, so a change made on a device that was offline is merged correctly when it reconnects. If two devices change the same setting at the same time, the same rule picks the winner on every device. You never end up with half of one value and half of the other.

### Payments need the device you hold

A request to pay, or to lock money for a capacity purchase, must be approved on a held device. Usually that is your phone.

The approval is a post-quantum signature over:

- the exact amount;
- the rail (DEBIT 555, CREDIT 777 or EQUITY 888);
- the currency (VFV);
- the payee;
- the memo;
- an expiry time.

A signature that has expired, was replayed, or came from a device that is not active and held is rejected. The home computer cannot spend your money by itself.

## 3. Buying capacity

Sometimes you need more than your devices can give: a large model on a trip, extra storage, or relay bandwidth. Then you can buy it from other people's machines. Anyone with spare capacity can sell it.

The four markets:

| Market | 1 unit |
|---|---|
| compute | 1 compute-hour |
| inference | 1,000 tokens from a large model |
| storage | 1 GB for the period |
| bandwidth | 1 GB relayed |

There is a separate order book for each region and delivery period (an hour, a day, a week or 30 days).

How the price is set:

- **Providers** post an ask: how many units, and the lowest price per unit they accept. That price is their fee, and they choose it.
- **Buyers** post a bid: how many units, and the highest price they will pay. The bid locks that amount from money the buyer already has. Nothing is lent, so there is no debt.
- **Clearing:** at regular intervals the market matches as many units as possible, where supply meets demand. **Everyone in the round trades at one price**: the middle of the range where no matched order would refuse and no unmatched order would want in. A provider who asked less still gets that price. A buyer who bid more still pays only that price. So the sensible thing is to state your true price.

No provider can take over the market. When three or more providers can sell at the clearing price, each one is first served up to 8/21 of the volume, about 38%. With two providers, the limit is half. A provider can go over its share only if nobody else can supply the rest. So a large provider cannot push small ones out by undercutting them by one unit. A market with a single provider is flagged as concentrated.

You pay only for what is delivered:

1. At clearing, the buyer's money for the matched units moves into escrow. Anything locked above the clearing price comes straight back.
2. The provider is paid unit by unit, against proof that the service was delivered.
3. When the period ends, money for undelivered units goes back to the buyer in full.

No interest, late fee or penalty rate is ever charged. On each payment a small tithe goes to the commons: about 1.618% (φ/100), computed exactly in integers. The rest goes to the provider.

Prices are in VFV minor units, with 100 minor units to 1 VFV. All the arithmetic uses exact integers.

Every buyer and provider who has the same signed orders computes the same result. A short digest of the round's orders lets them check this before they accept a contract.

## 4. Privacy

- **The content of your traffic is end-to-end encrypted.**
  - The handshake uses ML-KEM-1024 + X25519 for key exchange and ML-DSA-87 for signatures. There is a stronger optional level (`pq_matrix` MATRIX).
  - Records are encrypted with ChaCha20-Poly1305. Each direction has its own key, and replays are rejected.
  - A relay, a Wi-Fi network or an internet provider sees only encrypted frames.
- **No server is involved**, so no company holds your device list, your prompts or your settings. Your prompts go only to your own devices, unless you buy inference.
- **When you buy inference, the provider sees the prompt** it processes. Don't send anything to a market provider that you would not show a stranger. Running on your own devices avoids this.
- **What others can still see:** on a local network, other devices can see that ZXV frames are being sent. Each frame's header carries a mesh ID and a device ID, which are random and say nothing about you, and timing and sizes are visible too. Hiding this metadata is not done yet (see below).
- **Keys:**
  - The identity seed never leaves the device.
  - The pairing code and the QR secret are used once.
  - After a device is revoked, the other devices refuse it.

## 5. What is not done

These are the honest gaps as of this version:

- **The device list is not saved to disk yet.** After a restart, the phone makes a fresh mesh of one. The core can encode the signed roster (`dm_roster_encode`), but the apps do not store and reload it yet.
- **On-device inference is not wired in.** The library chooses which model fits and routes requests, but neither app loads a GGUF model or runs it yet. A local answer shows a placeholder.
- **The capacity market is not in the app screens.** It is complete and tested in C, including the devmesh glue that approves escrow on your held device. The phone screens only explain it.
- **Feed, calls and the full wallet are sample screens** on mobile. They are not yet connected to the social, call and payment modules.
- **The Android app was not built here.** The build machine could not reach `dl.google.com` (and `maven.google.com`, which redirects there), so the Android Gradle plugin, SDK and NDK could not be downloaded. What was checked:
  - The Kotlin UI and model code compile against Compose Desktop 1.7.3.
  - The JNI glue compiles for arm64.
  - It links with the core into a shared library that has no undefined symbols.
- **The iOS app has not been built.** It needs Xcode on a Mac, which this build machine does not have. `mobile/ios` is a Swift package written to the same C API, but no compiler has checked it.
- **The internet carrier is not bridged.** The apps carry frames over UDP on the local network. Over the internet, frames are meant to go through Carracho or EHOP, and the core already gives a session key for EHOP. This is not connected in the apps yet.
- **Metadata hiding:** device and mesh IDs and traffic timing are visible to anyone on the same network.
- **Sybil limits in the market:** the anti-monopoly cap counts providers, not people. Someone running many provider identities could get around it. Fixing this needs the identity layer (`kernel/src/ident`) or stake, and that is not done.
- **No agreement on order sets:** clearing is the same everywhere for the same orders, and the round digest lets two parties compare. But nothing yet makes sure every participant sees the same set of orders, and the gossip and signing of orders is left to the caller.
