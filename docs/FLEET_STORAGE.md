# Fleet storage architecture — the nine-server armada

The final deployment spec. Measured from the Contabo API (2026-10-17), not
remembered: each server's disk is split into four bounded stores, and the
release's every CID and shard lands on every server.

## The inventory (measured)

| Node | Region | IP | Product | Disk | CPU | RAM |
|---|---|---|---|---|---|---|
| IND | Asia (India) | 13.140.75.51 | Storage VPS 20 | 400 GB | 3 | 8 GB |
| EU | European Union | 173.249.40.169 | Storage VPS 20 | 400 GB | 3 | 8 GB |
| JPN | Asia (Japan) | 84.247.159.91 | Storage VPS 20 | 400 GB | 3 | 8 GB |
| US-c | United States (Central) | 195.26.240.140 | Storage VPS 10 | 300 GB | 2 | 4 GB |
| AUS | Australia (Sydney) | 46.250.247.153 | Storage VPS 10 | 300 GB | 2 | 4 GB |
| UK | United Kingdom | 81.0.221.238 | Cloud VPS 8 | 300 GB | 8 | 24 GB |
| US-e | United States (East) | 13.140.47.157 | Cloud VPS 6 | 200 GB | 6 | 12 GB |
| US-w | United States (West) | 62.146.173.107 | Cloud VPS 6 | 400 GB | 6 | 12 GB |
| SIN | Asia (Singapore) | 13.140.66.46 | Cloud VPS Plus 8 | 900 GB | 8 | 24 GB |

Total: 3,600 GB. SIN is the mainframe: the object storage anchor, the
warehouse, the bulk pinner.

## The four stores per server

Every server's disk (the ZXV_DATA partition / ZXVFS) divides into four
bounded stores:

1. **OS image store** — the container disc image (the qcow2 itself). Small,
   fixed, reserved exclusively for the operating system. Never touched by
   the other stores.

2. **The release pin store** — every CID of the OS release: the root, the
   discs, the world-computer interface files, every shard, recursively.
   This store is permanent and identical across all nine servers — the
   content-addressed replica set. Its size is the release's own.

3. **The information-trading store** — the dynamic bucket: IPFS content
   pinned for immediate availability, selected by the demand-pinning loop
   (velocity + wants) and the information economy. Its contents change
   with network demand; eviction is velocity-driven, never LRU. Bounded
   by what remains after stores 1, 2 and 4.

4. **The seed-file store** — every processed seed file (the parametric
   descriptors: ZXVG, font, harmonic, fractal, and the fabrication
   manifests) saved regardless of trading demand. Permanent, small, and
   the basis of the fabrication factory: the seed files ARE the
   compressed forms, so the network's effective capacity is bounded by
   the seeds, not the raw payloads.

The SIN node additionally hosts the object-storage anchor (the Filebase
bucket sync) and the warehouse tier; the UK/US-west/SIN nodes carry the
larger trading stores by capacity.

## The turnkey install

`zxv install <root CID>` on any server: the installer (tools/zxv-get,
the bucket's installer file) verifies the root, fetches the core disc by
CID, and the kernel's signed A/B update (abupdate) commits it — one
command from a CID, the same on bare metal, a VPS, or the desktop.

## User state

A user continues where they left off because their state is theirs:
the sovereign keypair lives in the browser's IndexedDB (or the desktop's
key store), the L2 bus resumes from its cursor (`/l2?after=`), and their
profile data is content-addressed — pinned on the fleet, fetched by key.
No server holds a password or a profile database; the identity is the
key, the state is the CIDs.

## The final sync (the last step)

The Filebase migration happens once, cleanly, after testing completes:
one S3-pipeline transfer of the complete build (no manual diffs), every
file where it belongs, the root CID and every shard pinned recursively on
Filebase, and then that CID list pinned on all nine servers. That is the
end-user state; nothing changes in it after.