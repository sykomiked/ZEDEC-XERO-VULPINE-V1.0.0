# ZEDEC pqOS — New Modules Architecture & Integration Guide

**Version:** 1.0.0+ (Post-Boot Legger/Panopticon/Legal Engine Integration)
**License:** SEL-3.3 — Streisand Engine License
**Author:** H.M. Michael-Laurence: Curzi (c)
**Organization:** 36N9 Genetics, LLC — Irrevocable, Interdimensional

---

## Table of Contents

1. [Boot Legger P2P Transport Layer](#1-boot-legger-p2p-transport-layer)
2. [Alternating Endianness Frequency Encryption](#2-alternating-endianness-frequency-encryption)
3. [Panopticon Surveillance Awareness Module](#3-panopticon-surveillance-awareness-module)
4. [25-Grid VPN Mesh Matrix](#4-25-grid-vpn-mesh-matrix)
5. [Algorithmic Legal Engine](#5-algorithmic-legal-engine)
6. [P2P Treaty Protocol](#6-p2p-treaty-protocol)
7. [Legal Precedent Sharing & Arbitrage](#7-legal-precedent-sharing--arbitrage)
8. [DAO Governance](#8-dao-governance)
9. [Panopticon + LPRES Legal Integration](#9-panopticon--lpres-legal-integration)
10. [Cross-Module Integration Map](#10-cross-module-integration-map)
11. [Build & Verification](#11-build--verification)

---

## 1. Boot Legger P2P Transport Layer

**Source files:**
- `kernel/src/bootlegger/bootlegger.h` — Protocol definitions, data structures, API
- `kernel/src/bootlegger/bootlegger.c` — Full implementation

**Concept:** OS-native client/peer/tracker P2P transport combining chat, file sharing, news, and calls in one direct-stream protocol -- no centralized choke points, peers run their own routes.

### Features

| Feature | Description |
|---------|-------------|
| Direct-stream peer sync | No centralized trackers; DHT-backed peer discovery via smap |
| TCP handshake | Magic string `TCPZEDEC` with version negotiation |
| Chat | Multi-channel IRC-style chat with encrypted channels |
| File transfer | Chunked, resumable, content-addressed (SHA-256 via smap) |
| News/Social feed | Threaded discussions with reply chains, social posts |
| Media streaming | VoIP (voice frames) and video frames with adaptive bitrate |
| Legacy bridges | IRC, NNTP, Gopher, Gemini, SMTP protocol compatibility |

### API Summary

```c
int bootlegger_init(bootlegger_node_t *node, uint16_t port);
int bootlegger_connect(bootlegger_node_t *node, uint32_t ip, uint16_t port);
int bootlegger_send_msg(bootlegger_conn_t *conn, bootlegger_msg_type_t type,
                     const void *payload, uint16_t len);
int bootlegger_chat_send(bootlegger_node_t *node, uint32_t channel_id,
                      const char *message);
int bootlegger_file_request(bootlegger_conn_t *conn, uint32_t file_id,
                         uint32_t start_chunk);
int bootlegger_news_create_thread(bootlegger_node_t *node, const char *subject,
                               const char *body);
int bootlegger_call_setup(bootlegger_node_t *node, uint32_t peer_id,
                       uint8_t call_type, uint32_t *call_id);
int bootlegger_voice_send(bootlegger_node_t *node, uint32_t call_id,
                       const uint8_t *frame, uint16_t size, uint8_t codec);
```

### Role-Based Access Control

| Role | Permissions |
|------|------------|
| Guest | Observe, chat, download |
| User | Chat, download, news |
| Contributor | Upload files, create news threads |
| Admin | Full server control |

### LPRES Integration

The Boot Legger P2P layer integrates with the 5-state paraconsistent logic core:
- **G= (ok)**: Normal packet flow
- **G+ (speculative)**: Delayed frame, speculative buffer
- **G0 (isolated)**: Corrupted frame, isolate and retransmit
- **G- (contradiction)**: Conflicting state, flag for resolution
- **G* (drop)**: Unrecoverable, graceful disconnect

### Garlic Routing

All P2P traffic is wrapped via PungentClove garlic routing for metadata obfuscation:
```c
int bootlegger_garlic_wrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len);
int bootlegger_garlic_unwrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len);
```

---

## 2. Alternating Endianness Frequency Encryption

**Embedded in:** `bootlegger.c` (freq_crypto_t structure)

### Concept

Standard processors operate in fixed little-endian or big-endian byte ordering. This module transforms endianness from a static compile-time flag into an active cryptographic weapon.

### Endianness Modes

| Mode | Description |
|------|-------------|
| `ENDIAN_LITTLE` | Static little-endian |
| `ENDIAN_BIG` | Static big-endian |
| `ENDIAN交替_16` | Alternate every 16 bytes |
| `ENDIAN交替_8` | Alternate every 8 bytes |
| `ENDIAN交替_4` | Alternate every 4 bytes |
| `ENDIAN交替_2` | Alternate every 2 bytes |
| `ENDIAN交替_1` | Alternate every byte |
| `ENDIAN_FREQ_HOP` | Frequency-hopping analog mode (sync matrix driven) |

### How It Works

1. **Binary-level bit-stream reversal**: At each position in the stream, the module determines whether to apply big-endian or little-endian byte ordering. In big-endian mode, individual bits within each byte are reversed.

2. **Analog frequency modulation**: Bytes are mapped to analog oscillation frequencies, modulated by a phase shift derived from a 32-byte synchronization matrix. The resulting waveform is mapped back to a byte value with XOR diffusion.

3. **DPI nullification**: Modern state-level firewalls and DPI engines rely on fixed signature matching. When a packet stream constantly alternates its underlying binary endianness, automated signature scanners parse total gibberish — the data looks like random analog noise.

### API

```c
static int freq_encrypt(freq_crypto_t *fc, const uint8_t *input,
                        uint16_t len, uint8_t *output);
static int freq_decrypt(freq_crypto_t *fc, const uint8_t *input,
                        uint16_t len, uint8_t *output);
```

---

## 3. Panopticon Surveillance Awareness Module

**Source files:**
- `kernel/src/panopticon/panopticon.h` — Types and API
- `kernel/src/panopticon/panopticon.c` — Implementation

### Purpose

Tells the user **who is watching them**, rates watchers on a **friendliness scale**, and provides **real-time transparency** about what the Panopticon itself is doing.

### Watcher Classification

| Type | Description | Default Friendliness |
|------|-------------|---------------------|
| Local User | Same machine | +50 |
| P2P Peer | Connected Carracho node | +20 |
| System Service | Internal OS service | +30 |
| Network Node | Private network range | -10 |
| External Entity | Unknown external | -20 |
| Government Actor | Classified as state-level | -50 |
| Corporate Actor | Corporate surveillance | -40 |
| Admiralty/Sovereign | Trusted sovereign entity | +60 |

### Friendliness Scale

```
-100 ──── -75 ──── -50 ──── -25 ──── 0 ──── +25 ──── +50 ──── +75 ──── +100
  │         │         │         │       │       │        │        │       │
 Hostile  Adversarial Hostile Suspicious Neutral Cooperative Friendly Trusted
 Threat                                                        Ally
```

### Threat Levels

| Level | Friendliness Range | Description |
|-------|-------------------|-------------|
| None | ≥ 0 | No threat |
| Low | -1 to -20 | Minor concern |
| Medium | -21 to -50 | Active surveillance |
| High | -51 to -80 | Hostile actor |
| CRITICAL | -81 to -100 | Immediate danger |

### Action Tracking

The Panopticon tracks what watchers are doing:
- Observing, Probing, Scanning, Connecting
- Requesting Data, Fingerprinting, Tracking
- Injecting, MITM Attack

Each action automatically adjusts the friendliness score.

### Self-Reporting

The Panopticon reports its own status transparently:
```c
int panopticon_self_report(panopticon_state_t *state,
                           char *buf, uint16_t buf_len);
```
Reports: active status, scanning state, logging state, watcher count, event count, and description.

---

## 4. 25-Grid VPN Mesh Matrix

**Source files:**
- `kernel/src/panopticon/panopticon_vpn.h` — Types and API
- `kernel/src/panopticon/panopticon_vpn.c` — Implementation

### Architecture

```
     Opt0         Opt1         Opt2         Opt3         Opt4
  +------------+------------+------------+------------+------------+
H0|>>FreeGate-US| FreeGate-CH| FreeGate-IS| P2P-Relay-01| Tor-Entry |
  +------------+------------+------------+------------+------------+
H1|>>Relay-DE  | Relay-SE   | Relay-JP   | P2P-Relay-02| I2P-Relay |
  +------------+------------+------------+------------+------------+
H2|>>MidRelay-CA|MidRelay-NO| MidRelay-SG| P2P-Relay-03| SSH-Tunnel|
  +------------+------------+------------+------------+------------+
H3|>>ExitRelay-FI|ExitRelay-PT|ExitRelay-NZ|P2P-Relay-04|Garlic-Exit|
  +------------+------------+------------+------------+------------+
H4|>>Exit-PA   | Exit-RO    | Exit-KR    | P2P-Exit-01| Tor-Exit  |
  +------------+------------+------------+------------+------------+

  >> = active node in current circuit
  Total paths: 3125 (5^5)
```

### Key Properties

- **5-Hop Deep Tunneling**: Every outbound packet traverses 5 sequential, independently encrypted nodes
- **5×5 Selection Array**: 5 options per hop = 3,125 possible circuit permutations
- **Auto-Rotation**: Mutates node sequence at user-defined intervals (default: 300s)
- **Auto-Routing**: Selects best path based on trust score + latency + log policy
- **Kill Switch**: Blocks all traffic if VPN drops
- **Free-Tier Defaults**: Uses secure free VPN/P2P relays out of the box

### Supported Protocols

| Protocol | Use Case |
|----------|----------|
| WireGuard | High-performance encrypted tunnel |
| OpenVPN | Legacy compatibility |
| IPsec | Enterprise VPN |
| SSH Tunnel | Stealth tunneling |
| Tor | Onion routing |
| I2P | Invisible internet |
| Garlic (PungentClove) | Native garlic routing |
| PLNP | pqOS native encrypted transport |

### LPRES Failsafe Integration

If any node experiences latency spikes, packet loss, or plummeting friendliness score, LPRES instantly reroutes to a healthy alternate node without dropping the session.

---

## 5. Algorithmic Legal Engine

**Source files:**
- `kernel/src/legal_engine/legal_engine.h` — All types and APIs
- `kernel/src/legal_engine/legal_engine.c` — Base engine (documents, agreements, nations)
- `kernel/src/legal_engine/legal_engine_ext.c` — Extended engine (treaties, precedents, DAOs, arbitrage, Panopticon integration)

### Base Engine

#### Nation Database
24 AU member states loaded from the ASCW diplomatic project, including:
- Country code, name, languages, colonial history
- Resources, conflicts, currency, CFA status
- Colonial root causes, foreign exploitation, national strengths

#### Document Generation
- Treaties, diplomatic packages, risk assessments, implementation plans
- 6-language support (English, French, Arabic, Portuguese, Kiswahili, Spanish)
- Golden ratio (φ) checksum sealing

#### User Agreement Templates (for Carracho P2P servers)

Users can write their own agreements for accessing their servers. Default templates provided.

```c
int legal_gen_user_agreement(legal_engine_t *engine,
                             legal_user_agreement_t *ua,
                             const char *server_name,
                             const char *operator_name,
                             legal_lang_t lang);
int legal_customize_agreement(legal_user_agreement_t *ua,
                              const char *field, const char *value);
```

Customizable fields: access_rules, data_policy, prohibited_uses, liability, dispute_resolution, termination, jurisdiction, custom_clauses, operator.

---

## 6. P2P Treaty Protocol

### Treaty Lifecycle

```
Draft → Negotiating → Signed → Ratified → Active → (Suspended/Breached/Terminated)
```

### Treaty Types

| Type | Description |
|------|-------------|
| Bilateral | Two-party agreement |
| Multilateral | Multi-party agreement |
| Non-Aggression | Mutual non-aggression pact |
| Data Sharing | Data sharing rights |
| Resource Allocation | Resource distribution |
| Mutual Defense | Defense cooperation |
| Commerce | Trade agreement |
| Extradition | Extradition treaty |

### Self-Executing Smart Contract Clauses

Each treaty clause can be marked as executable with:
- **Trigger condition**: What event activates the clause
- **Remedy action**: What action to take when triggered

```c
int treaty_add_clause(p2p_treaty_t *treaty, const char *title,
                      const char *body, uint8_t executable,
                      const char *trigger, const char *remedy);
```

### Autonomous Enforcement

When a breach is detected:
1. Treaty status changes to `BREACHED`
2. Remedy actions from executable clauses are triggered
3. After 3 breaches, treaty auto-terminates
4. All actions logged with golden ratio seals

---

## 7. Legal Precedent Sharing & Arbitrage

### Precedent Database

Nodes share legal precedents via the P2P mesh:
```c
int precedent_add(legal_engine_ext_t *engine, const char *citation,
                  const char *jurisdiction, const char *summary,
                  const char *ruling);
int precedent_search(legal_engine_ext_t *engine, const char *keyword,
                     legal_precedent_t *results, int max);
int precedent_share(legal_engine_ext_t *engine, uint32_t precedent_id);
```

### Jurisdictional Arbitrage

The engine continuously scans for optimal legal routing:
- Compares jurisdictions for transaction optimization
- Identifies arbitrage opportunities (legal advantage scores)
- Auto-executes routing through optimal jurisdiction when score > 30

Pre-seeded patterns include: US→CH, EU→IS, UK→PA, DE→SG, and sovereign SEL-3.3 routing.

---

## 8. DAO Governance

### Features
- Create DAOs with custom constitutions
- Configurable voting thresholds (0-100%)
- Dispute resolution mechanisms: LPRES, arbitration, or voting
- Inter-DAO treaty negotiation with standard clauses

```c
int dao_create(legal_engine_ext_t *engine, const char *name,
               const char *constitution, uint32_t voting_threshold,
               uint8_t dispute_mechanism);
int dao_treaty_negotiate(legal_engine_ext_t *engine, uint32_t dao_id_a,
                         uint32_t dao_id_b, treaty_type_t type,
                         const char *title);
```

### Standard Inter-DAO Treaty Clauses

1. **Mutual Recognition**: Both DAOs recognize each other's sovereignty
2. **Dispute Resolution**: LPRES-based paraconsistent resolution
3. **Breach Penalty**: Automatic access revocation, friendliness reduction, G0 isolation

---

## 9. Panopticon + LPRES Legal Integration

### Friendliness → Jurisdictional Liability

The Panopticon's friendliness score is translated into a legal liability index:
- Friendliness -100 → Jurisdictional Risk +100 (maximum liability)
- Friendliness 0 → Risk 0 (neutral)
- Friendliness +100 → Risk -100 (trusted, no liability)

### LPRES Legal Reasoning States

| State | Friendliness Range | Legal Meaning |
|-------|-------------------|---------------|
| G= (ok) | ≥ 0 | Legal framework clear, proceed |
| G+ (speculative) | -1 to -20 | Grey area, proceed with caution |
| G0 (isolated) | -21 to -50 | Hostile legal probe, isolate into shadow block |
| G- (contradiction) | -51 to -80 | Conflicting jurisdictions, flag for resolution |
| G* (drop) | -81 to -100 | Unrecoverable legal conflict, terminate |

### Autonomous Legal Response

When legal action is recommended, the engine generates automated formal notices:
- Formal notice of jurisdictional violation
- Access permission revocation
- G0 shadow block isolation
- Compliance audit logging with φ seals
- P2P mesh broadcast via Carracho

```c
int legal_auto_respond(legal_engine_ext_t *engine, uint32_t watcher_id,
                       char *response_buf, uint16_t buf_len);
```

### Compliance Audit

Full system audit covering all treaties, precedents, DAOs, arbitrage opportunities, and risk assessments:
```c
int legal_compliance_audit(legal_engine_ext_t *engine, char *buf,
                           uint16_t buf_len);
```

---

## 10. Cross-Module Integration Map

```
┌─────────────────────────────────────────────────────────────────┐
│                     ZEDEC pqOS Kernel                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │  Panopticon  │───▶│  Legal Engine │───▶│  LPRES (G0/G+)  │   │
│  │  (watchers)  │    │  (treaties)   │    │  (legal states) │   │
│  │  Friendliness│    │  Arbitrage    │    │  Shadow blocks  │   │
│  └──────┬───────┘    └──────┬───────┘    └──────────────────┘   │
│         │                   │                                    │
│         ▼                   ▼                                    │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │  VPN Mesh    │    │  Boot Legger │───▶│  PungentClove    │   │
│  │  25-grid     │    │  P2P Layer   │    │  Garlic Routing  │   │
│  │  5-hop       │    │  Chat/Files  │    │  Metadata obfusc.│   │
│  │  Auto-rotate │    │  VoIP/Video  │    └──────────────────┘   │
│  └──────┬───────┘    └──────┬───────┘                          │
│         │                   │                                    │
│         ▼                   ▼                                    │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │  Freq Crypto │    │  smap (DHT)  │    │  identity        │   │
│  │  Alt-endian  │    │  Content-addr│    │  Ed25519 auth    │   │
│  │  Analog wave │    │  Peer lookup │    │  Key management  │   │
│  └──────────────┘    └──────────────┘    └──────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Data Flow

1. **External entity contacts node** → Panopticon registers watcher, classifies type, assigns friendliness
2. **Panopticon reports to Legal Engine** → Legal Engine translates friendliness to jurisdictional risk
3. **Legal Engine evaluates via LPRES** → Determines legal state (G=/G+/G0/G-/G*)
4. **If hostile (G0/G-)** → Legal Engine issues autonomous response, isolates into shadow block
5. **All traffic routed through VPN Mesh** → 5-hop encrypted tunnel with auto-rotation
6. **P2P communication via Boot Legger** → Alternating endianness frequency encryption + garlic routing
7. **Treaty violations trigger smart contracts** → Automatic enforcement, access revocation

---

## 11. Build & Verification

### Build Targets

| Platform | Makefile | Output | Status |
|----------|----------|--------|--------|
| ARM64 | Makefile.arm64 | kernel_arm64.elf (474KB) | ✅ |
| RISC-V 64 | Makefile.riscv | kernel_riscv.elf (438KB) | ✅ |
| RISC-V 32 | Makefile.riscv32 | kernel_riscv32.elf (434KB) | ✅ |

### Symbol Counts

| Metric | Count |
|--------|-------|
| Total text symbols (ARM64) | 1,142 |
| Carracho P2P symbols | 40+ |
| Panopticon symbols | 25+ |
| VPN mesh symbols | 20+ |
| Legal engine symbols | 40+ |
| Total new module symbols | 131+ |

### VM Image Formats (per platform)

- Raw (.raw), QCOW2, VDI (VirtualBox), VHD/VHDX (Hyper-V), VMDK (VMware)
- PXE netboot tarball, UEFI firmware tarball, rootfs tarball
- Cloud-init ISO, bootable ISO (x86_64)

### Documentation Files

| File | Description |
|------|-------------|
| ZEDEC_API_REFERENCE.md | Complete API reference (220KB) |
| ZEDEC_ARCHITECTURE.md | System architecture overview |
| ZEDEC_P2P_CARRACHO_DESIGN.md | Carracho P2P design document |
| ZEDEC_QUICKSTART.md | Quick start guide |
| ZEDEC_TURNKEY_REPORT.md | Turnkey verification report |
| ZEDEC_NEW_MODULES.md | This document |
| modules/*.md | 46 per-module documentation files |

---

*This documentation is generated as part of the ZEDEC pqOS transparent documentation initiative. All modules are open-source under SEL-3.3 Streisand Engine License.*
