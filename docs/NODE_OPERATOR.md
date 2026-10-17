# Node operator guide — run a ZXV PoP node

The turnkey install: one command from the release, verified by sha256,
no IPFS daemon required.

## Install

```sh
curl -sSf https://zedec.s3.filebase.io/zxv-release/install.sh | sh
# or the tap:
brew tap curzi8889/tap && brew install zxv 36n9 9n63
```

## Boot the node

```sh
zxv daemon --port 8724            # the local PoP node (zxvd)
zxv status                        # telemetry: key=value lines
zxv bank --digest <hex>           # a seed digest -> bank/len/offset
zxv search --query "paper"        # CISEX keyword search (raw envelopes)
9n63 telemetry                    # the state plane counters
9n63 spaces                       # the shared spaces directory
36n9 resolve 36n9://ipfs/<CID>    # the Filebase gateway fallback
```

## The machine surface

Every tool answers `--ai-manifest` with its JSON command tree, so
autonomous agents discover and drive the suite without parsing human
text. The endpoints are raw and documented in `aipi.json`:

- `POST /bank` — digest in (32 bytes), `[status][bank u16][len u32][offset u32]` out.
- `POST /telemetry` — the node counters.
- `POST /cisex` — the content index, 168-byte UBH-168 envelopes.
- `POST /spaces` — the shared spaces directory.

## The ports

- `8724` UDP — the VNA: sessions, records, the 36n9 seed plane.
- `80` TCP — the door: the web app, `/bank`, `/telemetry`, `/cisex`, `/spaces`.

## Bare metal, VM, browser, desktop

The same release serves all four: the core discs (bare metal/VM), the
boot assets (browser), and the daemon (desktop). The root CID installs
everything: `ipfs add -r` the bucket, or fetch the discs by CID from
the gateway.