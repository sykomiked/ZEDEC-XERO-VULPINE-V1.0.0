<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# IPFS distribution and indexing

How a sealed ZXV root gets found. Everything here is transparent and
legitimate: the content is what it says it is, the links point where they
say they point, and no channel is used in a way its community would call
spam. The goal is that the content address resolves fast, everywhere, for
anyone who asks for it — and that the people who find it find honest
documents.

## 1. Make the CID resolvable (the technical half)

Availability is the real indexing. A CID nobody can fetch is a dead link no
matter how many places print it.

- **Seed nodes.** The five bootstrap VPS run IPFS with the root pinned
  recursively and provider records published on the DHT. Low-compute, always
  on, geographically spread — they are the first fetch.
- **Filebase bucket.** The whole tree (every shard CID, every directory CID,
  the root) pinned through the Filebase API. Filebase's public gateway then
  serves any path at `https://ipfs.filebase.io/ipfs/<ROOT>/<path>`.
- **Local node.** The build Mac's Kubo keeps the root pinned and providing
  so a third, independent provider exists while the fleet ramps.
- **Gateway warming.** Walk the main paths once through the big public
  gateways (`ipfs.io`, `dweb.link`, `filebase`) so their caches hold the
  doorway, `boot.html`, `boot.json` and the 00_base names before a user
  does.
- **IPNS.** Publish the root under an IPNS name (or DNSLink TXT on a domain)
  so the *stable* address survives re-cuts: `ipfs name publish /ipfs/<ROOT>`.
  Every public link should prefer the IPNS name over a raw CID once it
  exists — a CID is per-cut, the name is forever.

## 2. Tell the right people once (the human half)

Quality over quantity: a small number of posts, each honest and each
written for the place it lands.

| Channel | What to post | Why it's fair |
|---|---|---|
| The project's own GitHub release notes | The root CID, the IPNS name, the gateway link, the clone lines | Release notes are where links belong |
| `docs/` in the repository | `MANUAL.md`, `SYNOPSIS.md`, `BUCKET.md` | The documents are the announcement |
| awesome-ipfs (PR) | One line: "ZXV pqOS — an operating system sealed and shipped as an IPFS DAG" with the gateway link | A curated list the IPFS community maintains for exactly this |
| discuss.ipfs.tech, one post | A short technical write-up: an OS release as a clonable git bundle inside a UnixFS DAG | Its users genuinely care about DAG-shaped releases |
| r/ipfs, one post | Same content, plainer language | One honest post is welcome; repetition is the spam line |
| Hacker News *Show HN* | "Show HN: An OS you can git clone from IPFS and boot in a browser" | Legitimate use of the intended channel, owner-posted once |

Rules for every channel: say what it is and what it is not (early, emulated
boots, honest limits), link the doorway rather than pasting the CID bare,
post once, and let the owner hold the account — posts by the project's own
voice, not a fan-shaped one.

## 3. What not to do

- No mass-posting, no link farms, no SEO padding, no bot accounts. The
  indexing problem is solved by *availability and quality*, not volume.
- No keyword stuffing and no disguising links as something else — platform
  filters are right to catch those, and they would be right here.
- No paid "crypto listing" or airdrop-style promotion of `$YHVH`. The
  voucher is an internal accounting unit; marketing it as an asset would be
  both wrong and counterproductive.
- Nothing that automates posting. Each item above is a human action by the
  owner.

## 4. Measure it

- `ipfs dht findprovs <ROOT>` from a fresh node should return the five seeds
  and Filebase quickly.
- `curl -o /dev/null -w '%{time_total}' https://<gateway>/ipfs/<ROOT>/boot.json`
  on a few gateways, repeated after a day.
- `ipfs name resolve` on the IPNS name after publication.
