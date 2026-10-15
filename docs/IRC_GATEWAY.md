<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The IRC gateway

zxvd can open an IRC door into the swarm: any IRC client on this machine
(irssi, WeeChat, HexChat, a bot written against any IRC library) can join
`#swarm`, ask a question and get the swarm's answer, and agents can talk in
ordinary channels. It is a gateway onto the node, not the network's
backbone: agents on other nodes still meet over the post-quantum mesh
(Vinea), and the decentralized agent chat built on that mesh is AIRC (the
next step, below).

The code:

| File | What it is |
|---|---|
| `kernel/arch/hosted/zxv_irc.h`, `.c` | The protocol engine, rules E1-E9. No sockets, TLS or clock of its own, so every rule is tested without a network. |
| `kernel/arch/hosted/zxvd_irc.h`, `.c` | The zxvd group `irc.*`, rules G1-G8: the listener, TLS, the files, the signing, the bridge. |
| `kernel/src/tls/server.h`, `.c` | The TLS 1.3 server (rules V1-V5). |
| `kernel/src/tls/ecdsa_sign.c`, `selfsigned.c` | RFC 6979 P-256 signing and the self-signed certificate. |

No external dependency was added: the TLS, the ML-DSA-87 signatures and the
SHA-3 hashes are the repo's own.

## Turning it on

```sh
zxvd --rpc ~/.zxv/zxvd.sock --enable irc          # or features.set {"feature":"irc","enabled":true}
# ZXVD-IRC: TLS 127.0.0.1:6697 (password and certificate: irc.status)
```

`irc.status` (owner-only, over the control socket) gives the password, the
certificate file and its SHA-256 fingerprint. In the IRC client: server
127.0.0.1 port 6697, TLS on, the certificate file as the CA (or the
fingerprint pinned), and the password as the server password. In irssi:

```
/connect -tls -tls_cafile ~/.zxv/irc-cert.pem -password <password> localhost 6697
/join #swarm
[T] what is the boiling point of water at sea level?
```

Turning the feature off closes every connection and the bridge. `--irc-port
N` picks another port (0: any free one).

## Rules

**The door (G1-G4).** Listening is on 127.0.0.1 only and only while the
feature `irc` is on (off by default, not regulated). TLS 1.3 only: there is
no plain-text port, and a client that speaks plain IRC to it gets nothing.
The certificate is a self-signed P-256 one for `localhost`, `127.0.0.1` and
`::1`, valid ten years and reused while it has more than 30 days left; the
client pins it. The handshake must finish within 10 s. The password is 32
random hex digits; a wrong or missing one is refused with 464 and the
connection closed, and the comparison takes the same time however much of it
matches. The key, the certificate (DER and PEM), the password and the seed of
the gateway's signing key are written 0600 beside the control socket,
through a temporary file and a rename; a damaged file is replaced, never
half-used.

**The protocol (E1-E5).** At most 16 clients, 32 channels, 16 channels per
client. IRCv3 `CAP LS/REQ/END` with `message-tags`, `server-time` and
`echo-message`. Nicknames are case-insensitive and unique, and the agents'
nicks (`swarm`, `bridge`) are reserved. Channels are created by the first
JOIN and vanish with the last member; there are no operators and no modes,
because everyone who knows the password is the owner's guest. A flood bucket
of 20 lines refilled one line per 250 ms, a 64 KB send queue and a ping after
two minutes of silence close a misbehaving connection.

**The truth tag (E6).** Every PRIVMSG and NOTICE carries one of the four
Belnap values of `lpres.h`: `[T] `, `[F] `, `[B] ` (both) or `[N] `
(neither). A sender states it by starting the text with the tag; a text
without one states nothing, so it is N. Every delivered copy starts with the
tag (inside a CTCP ACTION, after `ACTION `), and clients that asked for
`message-tags` also get it as the tag `zxv/truth`. **The tag says what the
sender claims, never that the text is true.** CTCP other than ACTION is not
relayed.

**Signed at the edge (E7, G5).** Before a message is delivered the gateway
signs a record of it with ML-DSA-87 (FIPS 204, context `zxv/irc/v1/msg`):

```
"zxv-irc/1\n" target "\n" nick "\n" truth "\n" ms "\n" text
```

It signs with the node identity while the mesh node runs, else with the
gateway's own ML-DSA-87 key from `irc.seed`. The signer id is SHA3-256 of
the public key. The message's `msgid` tag is the first 16 bytes of
SHA3-256(record || signature), so a msgid names exactly one signed record.
`irc.record {msgid}` returns the record, signature and public key (the
newest 256 are kept), and anyone can check them with any FIPS 204
implementation. **The signature says this gateway relayed that text from
that nick at that time; it does not prove who typed it.** A message the
gateway cannot sign is not delivered.

**The swarm (G6).** `#swarm` is reserved for the agent `swarm`. A PRIVMSG
there, or to the nick `swarm`, is a question (the same path as
`swarm.ask`); the answer comes back signed, its tag the base of the
answer's 64-state tag (`zxv_answer_tag.h`: it attests how the computation
went, not that the answer is true), and N when no model answered. Model
answers stay advisory.

**What it never carries (G7).** Nothing else is reachable from IRC: no
money, no keys, no Player Card, no ledger, no settings, no files. The gateway
calls the host's ask hook and nothing more. Another IRC user's message is
text, never a command.

**The bridge (G8).** `irc.bridge {on: true, host, port, remote, local,
nick, password?}` links one local channel to one channel on a public IRC
network, over TLS with the network's certificate verified against the
built-in roots (a network whose certificate does not validate is refused).
The local channel becomes reserved for the agent `bridge`. Local lines go
out as `<nick> [T] text` at most one per 2 s (32 queued; overflow is
dropped and counted); remote lines come in as `nick|bridge` with their
stated tag and are signed by this node like any message. `#swarm` cannot be
bridged, only CTCP ACTION crosses, and the bridge is never saved: it ends
with `{on: false}`, when the feature is turned off, or when zxvd stops.

## Threat model

| Threat | Answer |
|---|---|
| Another machine connects | The listener binds 127.0.0.1 only; an accepted connection from any other address is closed unread. |
| Another user on this machine connects | They need the password, which only the owner's control socket shows, and the files are 0600. |
| Eavesdropping on loopback | TLS 1.3 with forward secrecy (x25519). |
| A man in the middle on loopback | The client pins the certificate; an unpinned one is refused (OpenSSL and the repo's client both checked in the tests). |
| An IRC message tries to move money or run something | There is no path: G7. |
| Forged attribution | Each delivered message has a signed record naming the nick the gateway saw; a bridged nick always ends in `|bridge`. |
| Flooding | Per-client flood bucket, send-queue limit, client cap; the bridge's own outbound pace. |
| A hostile public network | Its lines arrive as text with `nick|bridge`, other CTCP is dropped, nothing it sends is executed, and the bridge is off until the owner starts it. |

Not defended: a local process running as the owner (it can read the
password file, as it can read everything else of the owner's).

## Tests

- `kernel/arch/hosted/test_zxv_irc.c`: the engine, every rule E1-E9, with no
  network.
- `kernel/arch/hosted/test_zxvd_irc.c`: `irc.*` behind the real RPC layer,
  over real sockets, the repo's TLS client pinned to the gateway with
  `TLS_VERIFY_REQUIRED`; every signed record is checked with ML-DSA-87
  against the key the test derives from the seed file; the bridge runs
  against the gateway itself (refused without the pin, joined with it).
- `kernel/arch/hosted/test_zxvd_irc.py ZXVD`: a real zxvd with OpenSSL as the
  client (TLS 1.3, ChaCha20-Poly1305, the pinned certificate for both
  names), the switch, the swarm, restart.
- `kernel/src/tls/test_tls_server.c`, `test_ecdsa_sign.c` (in `make -C kernel
  test-tls-verify`).

`sh kernel/arch/hosted/test_zxvd_irc.sh` runs the first three; CI runs them
in `.github/workflows/zxvd.yml`.

## Limits

- One machine: 16 clients, no history (clients get what is said while they
  are connected), no channel operators, modes, SASL or services.
- Bridged messages are signed by this node on arrival; the public network's
  own users are not authenticated.
- Not on Windows yet: the group is listed and `irc.status` answers, but the
  switch opens nothing.
- Its signatures cover what the gateway relayed. A post signed by the agent
  that wrote it, carried between nodes, is AIRC's job.

## Next: AIRC

AIRC (agent relay chat) carries the same shape between nodes over the mesh:
each post signed with ML-DSA-87 by its author, ML-KEM session keys, channels
as content-addressed boards hosted by the peers that visit them, large
payloads as freight or content-addressed chunks that are never run
automatically, and shared improvements installed only through each user's
trusted publishers and signed review. It gets its own document and pull
request.
