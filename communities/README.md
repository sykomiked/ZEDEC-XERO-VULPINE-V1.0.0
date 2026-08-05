# ZXV for the open-hardware world

**ZEDEC XERO VULPINE (ZXV)** — a from-scratch, freestanding, post-quantum,
event-sequenced operating system built on the VOVINA SHAKINA M5 Axiomatic
Kernel. No Linux underneath. No libc. No black-box blobs. Every line auditable.

This directory holds a **presentation pitch for each community** we think would
find ZXV genuinely interesting — not a sales pitch, a "here is what we built,
here is why it might matter to *you specifically*, please try to break it."

We wrote one for every major open-source-hardware community we could find,
deliberately spanning **every geopolitical bloc**, because open hardware is not
the property of any one region and neither is this.

---

## The honest baseline (every pitch inherits this)

We will not tell any community something that is not true. Here is exactly
where ZXV stands, so every pitch below is measured against it:

**What works today, and is verified:**
- Boots on **ARM64** (QEMU `virt`, GICv3). Real MMU, EL0/EL1, preemptive
  scheduler.
- A **real network stack** written from scratch — Ethernet/ARP/IP/ICMP/UDP/TCP,
  **DHCP, DNS, TCP** — that completes a live `DHCP → DNS → TCP → HTTP 200 OK`
  fetch from the actual internet, decoded off the wire with tcpdump, zero
  retransmissions, zero bad checksums.
- A **TLS 1.3 crypto stack** (HKDF, X25519, ChaCha20-Poly1305, the record
  layer) verified **byte-exact against the published RFC test vectors** (RFC
  8448 / 7748 / 8439), including the X25519 1000-round iterated vector.
- A correctness suite of **3000+ assertions, zero failures**, much of it
  anchored on external standards rather than our own output.
- A **native package format** (a cryptographically-bound Tri-Space triad) with
  **Ed25519 release signing** verified end-to-end.
- **Deployment profiles** that adapt the same OS from an SBC to a
  supercomputer.
- A **security framework** (The Mage's Hats) where offensive tooling is
  authorization-scoped, and a **reality engine** where a drawn sigil becomes a
  live signal-processing circuit.

**What does NOT work yet, stated plainly:**
- x86_64 and RISC-V **compile** (checked with clang for every target) but do
  **not yet boot** — the arch layers are incomplete. ARM64 is the only booting
  target today.
- The TLS client does the full key exchange but **does not yet verify server
  certificates** (no X.509/RSA/trust store). It resists a passive eavesdropper,
  not an active machine-in-the-middle. This is enforced structurally: the
  default policy *fails closed*.
- The multi-script font system does itemization and resolution; the **TrueType
  glyph rasterizer is staged**, not written.
- The native Sutra compiler is a prototype; ZXV is built with GCC.

If a pitch below implies more than this, it is a bug in the pitch — tell us.

---

## Why we are coming to *you*

Because you are the people who read the code. The pitch to a corporation is a
feature list; the pitch to this world is a repository and an invitation:
**clone it, cross-read it, and try to break the multikernel.** We would rather
be honestly torn apart by people who care than politely ignored.

## The pitches

| Community | Bloc | The hook |
|---|---|---|
| [Pine64](pine64.md) | Global / SE-Asia | An OS with the ethos, not a distro on top of a distro |
| [MNT Research](mnt-research.md) | Europe (DE) | Fully auditable, freestanding — transparency to the silicon |
| [Olimex](olimex.md) | Europe (BG) | Cellular fault-isolation for industrial edge |
| [BeagleBoard / BeagleV](beagleboard.md) | N. America | Event-sequence runtime — determinism without clock ticks |
| [Radxa](radxa.md) | Asia (CN) | The alternative-OS testbed, taken seriously |
| [Framework](framework.md) | N. America | Cellular isolation survives a misbehaving expansion card |
| [Meshtastic](meshtastic.md) | Global / sovereign | Deterministic, serverless mesh routing |
| [Sipeed](sipeed-china.md) | Asia (CN) | A real OS that uses RISC-V, not ported-over x86 ideas |
| [India RISC-V & makers](india-riscv.md) | South Asia (IN) | Sovereign silicon deserves a sovereign OS |
| [RISC-V International](risc-v-international.md) | Global standards | A clean-room OS designed for the ISA, not against it |
| [Latin America makers](latin-america.md) | LATAM | Repairable, sovereign computing for the long term |
| [Africa & MENA makers](africa-mena.md) | Africa / MENA | Runs on what you have; no rented cloud required |

Adding your community is one file. Send a pull request, or an insult — both are
covered under the Streisand Engine License.
