<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Protocol twins: a peer-to-peer AI equivalent for every networking protocol

Every networking protocol from the 1950s to today gets an AI twin in Web 4.
A twin does the job its ancestor did, but:

- it is **fully peer to peer**: no server, relay, directory or gateway has
  authority, and two machines on a cable can use it with nothing else running;
- it is **post-quantum end to end**: ML-DSA-87 identities and signatures,
  hybrid ML-KEM-1024 + X25519 sessions;
- it is **fully auditable**: every action is a hash-chained, signed log entry
  that anyone holding the entries can replay;
- every record carries its **paraconsistent tag** (T, F, B or N), so a
  contradiction is held as BOTH instead of being silently resolved.

This document is the catalogue. Section 1 is the pattern every twin follows.
Section 2 lists the protocols by era and function, with the twin for each and
its status. Section 3 is the build order. The first twin built is **AFTP**,
the file transfer twin (section 4, code in `kernel/src/aproto/`).

Twin names put an A (for AI and agent) in front of the protocol they replace,
the same way IRC becomes AIRC. A twin's numeric id is the IANA port of the
protocol it replaces where that protocol has one (AFTP is 21, ASMTP is 25),
so ids never need a registry of their own.

## 1. The twin pattern

Every twin is built from the same parts. None of them is new: they are the
Vinea overlay (`docs/VINEA.md`), the IPFS node (`docs/IPFS_NODE.md`),
freight packets, the Web 4 agent layer (`docs/WEB4.md`) and the paraconsistent
ledger. A twin adds only the records and the state machine its job needs.

| Rule | What it means |
|---|---|
| **P1 Identity** | A peer is its Vinea NodeID, `SHA3-256(ML-DSA-87 public key)`. No accounts, no usernames that a server hands out. Human-readable names are signed claims (ANAME, below), never authority. |
| **P2 Transport** | Twin records ride inside Vinea sessions (hybrid PQ handshake, ChaCha20-Poly1305 records, replay window). Discovery is the Vinea DHT, LAN discovery and cached node records. There is no bootstrap server. |
| **P3 One parser** | Every twin record is declared once as a `vna_schema_t` and goes through the one audited walker (rules S1 to S8 of `docs/VINEA.md`). No hand-written parsers. |
| **P4 Signatures** | Every record that outlives its session (a listing, an offer, a post, a checkpoint) is signed with its own context string (`aproto/v1/<twin>-<record>`), so a signature for one purpose is never accepted for another. |
| **P5 Content addressing** | Payloads are named by hash: Vinea file roots, IPFS CIDs and freight packets. Anything addressed by hash can be served by any peer that holds it, so a publisher can go offline and its content stays reachable. Large payloads never sit inside messages. |
| **P6 Tags** | Every record and every log entry carries an lpres tag: T verified, F refused or failed, B contradictory evidence (held, never resolved by guessing), N pending or unknown. The numeric values are those of `lpres_state_t`. |
| **P7 Audit** | Every twin logs to `ap_log` (section 4.1): `head_k = H(head_{k-1}, entry_k)`, with ML-DSA-87 signed checkpoints. An action that cannot be logged is not taken ("no audit, no action"). Two signed checkpoints with the same sequence and different heads are a fork, tagged B. |
| **P8 Consent** | Nothing arrives uninvited. A push in an old protocol (an upload, a mail, a page) becomes a signed offer the receiver may take or decline. Another agent's message is data, never a command, whatever its wording. |
| **P9 Agreements** | What a peer serves, to whom and how much is its Vinea agreement (`vna_agree.h`), checked before any work and charged on success. |
| **P10 Money** | No twin creates a payment path. Value moves only through the existing economy (Vinea receipts, pay ledger, settlement spine). |
| **P11 Gateways** | A bridge to the legacy protocol (a real FTP or IRC server) is optional, off by default, and marks everything that crosses it as unsigned outside data. |
| **P12 Bearers** | Transport scope is compatibility only: serial lines, Ethernet, Wi-Fi, cellular, fibre, satellite and radio are bearers we run over, not networks we operate. |
| **P13 Amateur radio** | Over amateur bands a twin sends only cleartext, signed emergency traffic: no encryption, no payments, no settlement. A signature is allowed because it does not obscure meaning. |

## 2. The catalogue

Status: **Built** means the code is on `main`; **This PR** means it ships in
the pull request that adds this document; **Owned** means another workstream
is building it; **Planned** means it is designed here and not yet built.

### 2.1 1950s and 1960s: wires, teleprinters and the first data links

| Protocol (year) | What it did | Twin | Peer-to-peer design | Status |
|---|---|---|---|---|
| Telex and teleprinter networks (1930s, at scale in the 1950s) | Typed messages between subscriber machines | **AMAIL** (store and forward) | Signed messages held in the Vinea offline spool (`vna_node.h`) until the recipient is reachable | Planned |
| SAGE data links (1958) | Radar tracks from site to site | **APUB** (telemetry) | Signed, tagged observations published under a topic key. Logistics and mapping only (project scope) | Planned |
| Bell 101 modem and RS-232 serial (1958 to 1960) | Bits over phone lines and cables | Bearer (P12) | Vinea frames (UBH-168 framed) run over any byte pipe | Built (framing) |
| Airline reservation networks (1960) | Shared booking records | **ALEDGER** | The paraconsistent ledger with signed receipts | Built (ledger) |
| BISYNC (1967), SDLC and HDLC (1970s) | Framed, checked link transfers | Bearer (P12) | UBH-168 frames carry the check | Built |

### 2.2 1970s: ARPANET and the first applications

| Protocol (year) | What it did | Twin | Peer-to-peer design | Status |
|---|---|---|---|---|
| NCP (1970), then TCP/IP (1974, 1981) | Host-to-host connections | Bearer (P12) plus Vinea sessions | Sessions are the twin's connection; IP is only a bearer | Built |
| Telnet (1969, RFC 854 in 1983) | Remote terminal | **ASHELL** | Only between devices of the same owner, under an explicit capability grant, with every command logged and human-approved; never a way for one agent to run commands on another's machine (P8) | Planned |
| FTP (RFC 114 in 1971, RFC 959 in 1985) | List, get and put files | **AFTP** | Signed listings, content-addressed files fetched from many peers at once, uploads as consented offers, resumable, every chunk verified and logged | **This PR** |
| Email: SNDMSG (1971), RFC 561 (1973) | Personal messages | **AMAIL** | Signed, encrypted to the recipient's key, delivered through the spool; large bodies by content hash | Planned |
| Ethernet (1973) | Local links | Bearer (P12), LAN discovery | `vna_lan.h` finds peers on the segment | Built |
| X.25 (1976) | Public packet networks | Bearer (P12) | | Bearer |
| UUCP (1976) | Batched mail and files between machines that dial up | **AMAIL**, **AFTP** | The spool already does store and forward; AFTP offers carry the files | Planned (AMAIL) |
| Finger (1977) | Who is online, and their plan | **APRES** | A signed presence record in the DHT, published only if the owner opts in | Planned |
| XMODEM (1977), Kermit (1981), ZMODEM (1986) | File transfer over serial lines with checks | **AFTP** | Merkle-verified chunks make every transfer checked and resumable on any bearer | **This PR** |
| BBS software (1978) | Boards people dial into | Vinea public spaces | Spaces anchored by a content id and hosted by the peers that visit them | Built (spaces PR) |
| Usenet (1980), NNTP (1986) | Distributed discussion groups | **ANEWS** | Groups are spaces; posts are signed and content-addressed; peers flood-fill what they choose to carry | Planned |

### 2.3 1980s: the Internet's standard services

| Protocol (year) | What it did | Twin | Peer-to-peer design | Status |
|---|---|---|---|---|
| ICMP ping, traceroute (1981, 1987) | Is a host up; what path | **APING** | Vinea PING with signed PONG and round-trip time logged | Built (PING) |
| SMTP (1982), POP (1984), IMAP (1986) | Send, fetch and keep mail | **AMAIL** | One twin covers all three: the spool sends, the recipient's own devices keep the mailbox and sync it between themselves | Planned |
| Whois (1982) | Who holds a name or address | **ANAME** | Signed name claims; ownership disputes held as B | Planned |
| DNS (1983) | Names to addresses | **ANAME** | Names are signed DHT records under the owner's key; a name two parties both claim stays B until the owner's chosen arbiters sign | Planned |
| SMB (1983), NFS (1984) | Shared network folders | **AFTP**, then **ASYNC** | A shared folder is an AFTP listing that the owner re-signs on every change | **This PR** (listing) |
| X11 (1984) | Remote graphical display | **ASHELL** (display) | Same consent and logging rules as ASHELL | Planned |
| X.400 (1984) | Formal message handling | **AMAIL** | | Planned |
| FidoNet (1984) | Hobbyist store-and-forward mail between BBSes | **AMAIL** | The spool is FidoNet's idea with signatures | Planned |
| AX.25 packet radio (1984) | Data over amateur radio | **AHAM** | Cleartext signed emergency traffic only (P13) | Planned |
| NTP (1985), Time protocol (1983) | Clock synchronisation | **ATIME** | Several peers' signed timestamps; the median of the agreeing set, outliers tagged F, a split tagged B. SNTP already exists in `kernel/src/timesvc` | Planned |
| Kerberos (1988), RADIUS (1991) | Central login tickets | **AKEY** | Capability tokens signed by the owner's key; no central ticket server | Planned (Web 4 consent tokens built) |
| IRC (1988) | Channels and nicknames for live chat | **AIRC** | Owned by the IRC gateway thread: phase 1 is an IRC gateway, phase 2 AIRC on the PQ mesh with large payloads and a public bulletin | Owned |
| SNMP (1988) | Monitoring and management | **AMON** | Signed, tagged readings published to the owner's own devices; management actions need the owner's approval | Planned |
| BGP (1989), RIP, OSPF | Routing between networks | Vinea Kademlia routing | Routing by XOR distance over verified contacts | Built |
| Syslog (1980s, RFC 5424 in 2009) | System logs to a collector | **ALOG** | `ap_log` itself: every node keeps its own chain and can hand signed checkpoints to whoever audits it | **This PR** (`ap_log`) |

### 2.4 1990s: the web, encryption and file sharing

| Protocol (year) | What it did | Twin | Peer-to-peer design | Status |
|---|---|---|---|---|
| Archie (1990), WAIS (1990) | Search for files and documents | **AFIND** | Search over signed AFTP listings that peers chose to index; results tagged by how many independent listings agree | Planned |
| HTTP (1991) | Hypertext pages | **AWEB** | Pages are content-addressed bundles, signed by their author, fetched from any holder. Web 2 bridging already exists in `kernel/src/web4` | Planned (bridge built) |
| Gopher (1991) | Menus of documents | **AWEB** | A Gopher menu is an AFTP listing | **This PR** (listing) |
| MIME (1992) | Typed attachments | Part of every twin | Each AFTP entry carries a kind; payloads are content-addressed | **This PR** |
| APRS (1992) | Positions and short messages over amateur radio | **AHAM** | P13 applies | Planned |
| LDAP (1993) | Directories of people and resources | **ADIR** | Signed directory records in the DHT and in spaces | Planned |
| DHCP (1993) | Address assignment | Not needed | Peers are their NodeIDs; addresses are bearer details | Not applicable |
| SSL (1994), TLS (1999) | Encrypted connections | Vinea sessions | Hybrid post-quantum sessions; the repo's own TLS remains for Web 2 bridging | Built |
| SSH (1995) | Secure shell and file copy | **ASHELL**, **AFTP** | SFTP and SCP are AFTP; the shell is ASHELL | **This PR** (files) |
| RTP (1996), SIP (1996), H.323 | Voice and video calls | **ACALL** | Session setup by signed offer; media over Vinea sessions with the call congestion control in `kernel/src/call` | Planned |
| rsync (1996) | Efficient folder sync | **ASYNC** | Merkle trees make a diff the set of chunk CIDs the other side lacks | Planned (after AFTP) |
| RDP (1996), VNC (1998) | Remote desktop | **ASHELL** (display) | Same rules as ASHELL | Planned |
| IPP (1999), LPD (1990) | Printing | **APRINT** | A print job is an AFTP offer to the printer's node, which the owner's agreement accepts or refuses | Planned |
| WebDAV (1999) | Editing files on a web server | **AFTP**, **ASYNC** | | Planned |
| XMPP (1999) | Chat and presence | **AIRC**, **APRES** | | Owned (AIRC) |
| RSS (1999), Atom (2005) | Feeds | **ANEWS** | A feed is a signed, sequenced listing | Planned |
| MQTT (1999), AMQP (2003) | Publish and subscribe | **APUB** | Topics are DHT keys; messages signed and tagged; grid and clinical topics read-only and advisory | Planned |
| Centralised file-sharing directories (1999) | Find files on other people's machines | **AFIND**, **AFTP** | No central index: providers announce in the DHT | Partly built (DHT providers) |

### 2.5 2000s: swarms, ledgers and the social web

| Protocol (year) | What it did | Twin | Peer-to-peer design | Status |
|---|---|---|---|---|
| Gnutella (2000) | Decentralised file search | **AFIND** | | Planned |
| Swarm file sharing (BEP 3, 2001) | Many peers each send pieces | **AFTP** | Multi-source fetch with Merkle-proven chunks; a peer that sends one bad chunk is dropped and logged | **This PR** |
| Kademlia (2002) | Distributed hash table | Vinea routing | | Built |
| mDNS and DNS-SD (2002, RFC 6762 in 2013) | Find services on the LAN | Vinea LAN discovery | | Built |
| Bitcoin (2008) | Public ledger and payments | Economy (P10) | Bridged through `kernel/src/web4` (Web 3 side); no new payment path | Built (bridge) |
| OAuth (2010), OpenID Connect (2014) | Delegated login | **AKEY** | Bridged in `kernel/src/web4` for Web 2 logins | Built (bridge) |
| WebSocket (2011), WebRTC (2011) | Live browser channels and calls | **ACALL**, **AWEB** | | Planned |
| Double ratchet messaging (2013) | Forward-secret chat | **AMAIL**, **AIRC** | Session rekeying by fresh handshakes; spool messages encrypted per recipient | Planned |
| CoAP (2014) | Constrained-device requests | **APUB** | | Planned |
| Matrix (2014) | Federated rooms | **AIRC** | | Owned |
| IPFS (2015) | Content-addressed files | The IPFS node | Every AFTP chunk has a real IPFS raw-block CID | Built |
| HTTP/2 and gRPC (2015) | Multiplexed calls | **AIPI** | Content-addressed functions with verified results | Built |
| Ethereum (2015) | Programmable ledger | Economy (P10) | Bridged in `kernel/src/web4` | Built (bridge) |
| WireGuard (2016) | Simple VPN tunnels | Vinea sessions | | Built |
| ActivityPub (2018), AT Protocol (2023), Nostr (2020) | Federated and decentralised social posts | Vinea public spaces, **ANEWS** | Bridged in `kernel/src/web4` | Built (bridge) |
| DNS over HTTPS (2018) | Private name lookups | **ANAME** | Lookups go to the DHT over sessions | Planned |

### 2.6 2020s: modern transport and agent protocols

| Protocol (year) | What it did | Twin | Peer-to-peer design | Status |
|---|---|---|---|---|
| QUIC and HTTP/3 (2021) | Fast encrypted transport over UDP | Vinea sessions over datagrams | Out-of-order records with a replay window already | Built |
| MLS (2023) | Group key agreement | **AIRC** (groups) | | Owned |
| Model Context Protocol (2024) | Tools and context for one model | **AIPI**, Web 4 capability manifests | Tools are content-addressed functions; manifests are signed | Built |
| Agent-to-agent task protocols (2025) | Agents hand tasks to other agents | Web 4 envelopes, **AIPI** | Signed envelopes, consent tokens and verified results | Built |
| Chat for agents with large payloads | (new) | **AIRC** | | Owned |

### 2.7 Bearers (transport scope: compatibility only)

Serial lines, telephone modems, X.25, Ethernet, Wi-Fi, Bluetooth, cellular
(2G to 5G), fibre, satellite (including LEO constellations), LoRa and
amateur radio are bearers. Vinea frames run over them; we do not operate
them. Over amateur bands rule P13 applies to every twin.

## 3. Build order

The order follows what other twins need first:

1. **`ap_log`** (this PR): the audit chain every twin logs to.
2. **AFTP** (this PR): files. ASYNC, APRINT, AWEB and AFIND reuse its signed
   listings and multi-source fetch.
3. **AIRC** (IRC gateway thread): chat and the public bulletin.
4. **AMAIL**: store and forward, on the existing spool.
5. **ANAME** and **APRES**: names and presence in the DHT.
6. **ATIME**: peer time with tagged outliers.
7. **APUB**, **AMON**: publish and subscribe, monitoring.
8. **ANEWS**, **AFIND**, **AWEB**: feeds, search and pages.
9. **ACALL**: calls, on the call congestion control.
10. **ASHELL**: last, because it is the most dangerous; owner-only with human
    approval.
11. **AHAM**: the amateur-radio profile of AMAIL and APUB under rule P13.

## 4. What this PR builds

### 4.1 `ap_log`: the audit chain (`kernel/src/aproto/ap_log.h`)

| Rule | Meaning |
|---|---|
| L1 | An entry is a schema record: owner, sequence, previous head, time, twin id, event, tag, peer, subject (a file root or content id), amount (bytes or units) and a status code. |
| L2 | `head_0 = H("aproto/v1/genesis", owner)`; `head_k = H("aproto/v1/log", entry_k bytes)`, and entry k names `head_{k-1}` as its previous head. Changing, dropping, inserting or reordering any entry breaks every later head. |
| L3 | Sequence numbers start at 1 and rise by exactly one. Time never goes backwards: an earlier clock reading is recorded as the last time seen. |
| L4 | The library keeps only the head and hands each entry's bytes to the host's sink, which stores them. If the sink fails, the entry is not counted and the caller must not act ("no audit, no action"). |
| L5 | A checkpoint is `{owner, seq, head, time}` signed with ML-DSA-87 under `aproto/v1/log-head`. Any peer can hold checkpoints and later demand the entries up to them. |
| L6 | An auditor replays the entries from genesis (`ap_audit_feed`) and compares the result with a checkpoint. Two checkpoints with the same owner and sequence but different heads are a fork, tagged B. |

### 4.2 AFTP: the file transfer twin (`kernel/src/aproto/aftp.h`)

| FTP command | AFTP equivalent |
|---|---|
| `LIST`, `NLST`, `MLSD` | A **listing**: up to 32 entries (path, file root, size, chunk size, kind, tag), signed by the publisher, with a sequence number and an expiry. Any peer may relay it; the signature makes relaying safe. |
| `CWD`, `MKD` | Paths inside a listing (`docs/readme.txt`). |
| `DELE`, `RNFR`/`RNTO` | A new listing with a higher sequence number. Nothing is deleted from anyone else's machine. |
| `RETR` | A **get**: chunks fetched from up to 8 peers at once, each verified against the file root by its Merkle path before it counts. |
| `REST` | Resume: the receiver keeps a bitmap of verified chunks. |
| `STOR`, `APPE` | An **offer**: signed, addressed to one peer. It waits in the receiver's inbox as N until the receiver takes it (then it becomes a get) or declines it. |
| FXP (server to server) | Any holder of the chunks can serve them; the receiver picks its sources. |
| `PASV`, `PORT` | Not needed: sessions and NAT traversal are Vinea's. |

Listing rules:

| Rule | Meaning |
|---|---|
| D1 | A listing verifies only if its publisher is `SHA3-256` of the key that signed it, the signature verifies under `aproto/v1/aftp-dir`, it has not expired and it was not created in the future (5 minutes of skew allowed). |
| D2 | Every path is relative, uses `/` between parts, has no empty, `.` or `..` part, no backslash, no control character (C0, DEL or C1), no bidirectional override (which can make `txt.exe` display as something else), at most 128 bytes, and is valid UTF-8. A listing cannot point outside the folder the receiver chose. |
| D3 | Paths are in strictly increasing byte order, which makes them unique and the encoding canonical. |
| D4 | Every entry's chunk size and size give a valid chunk count. |
| D5 | A receiver remembers the highest sequence it accepted per publisher and share. An older sequence is a rollback (F). The same sequence with different contents is equivocation: the first listing is kept, the share is tagged B and stays marked. |

Transfer rules:

| Rule | Meaning |
|---|---|
| G1 | A chunk is requested from one source at a time. Only that source's answer for that index is accepted; anything else is unsolicited and refused. |
| G2 | A chunk counts only after its Merkle path proves it into the file root (rules F3 to F5 of `vna_file.h`). |
| G3 | A source that sends one chunk that fails the proof is dropped for the rest of the transfer. A chunk under a Merkle root is either right or wrong, so one failure is proof. |
| G4 | A request that gets no answer within the timeout is given to another source; the slow source is not dropped. |
| G5 | Every verified chunk, every refused chunk and the completed file are logged with the source that sent them. |
| O1 | An offer verifies only if it is addressed to this node, signed by its sender under `aproto/v1/aftp-offer`, within 5 minutes of now, and its nonce is new. |
| O2 | A verified offer waits as N. Taking it starts a get with the sender as the first source; declining it logs F. Nothing is fetched or run because an offer arrived. |
| S1 | Serving checks the node's Vinea agreement first. Every served chunk and every refusal is logged; if the log cannot take the entry, the chunk is not sent. |

Nothing AFTP receives is ever run. An entry's kind is a label for the
interface, not permission.

### 4.3 Honest limits

- AFTP is a library of pure state machines, like Vinea. A host binds it to
  sessions, to storage and to the DHT provider lookups; the zxvd methods for
  that are the next step.
- Logging every chunk adds about one per cent to the bytes moved (one 163-byte entry
  per 16 KiB chunk).
- The audit chain proves that a log was not changed after a checkpoint was
  shared. It cannot prove that a node logged an event it chose to hide before
  any checkpoint covered it. Cross-checking two parties' logs of the same
  transfer is how that is caught, and it is not automated yet.
- A listing holds 32 entries. Larger folders are listings of listings (an
  entry whose kind is `LISTING` points at another signed listing).
