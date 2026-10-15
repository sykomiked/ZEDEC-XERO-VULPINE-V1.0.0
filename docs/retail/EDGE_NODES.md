# Edge nodes and edge clusters

Roadmap steps R4.4 (edge inference node) and R4.5 (edge cluster).

An **edge node** is a small, always-on box that runs `zxvd`, the ZXV daemon, with no window
and no desktop: a Raspberry Pi 5, a Jetson-class board or an x86_64 mini PC. It answers
requests from your own devices and, if you choose, from other ZXV nodes on your network.

An **edge cluster** is several edge nodes sharing the work. Each node says what it can do, every
node runs the same rules over what it has heard, and they all reach the same answer about who
does what, without a central server.

This page says how to build and install a node, how nodes form a cluster, and, at the end,
exactly what was tested and what was not.

## What you need

| Board | Profile | CPU | Memory the service may use | Suggested models |
|---|---|---|---|---|
| Raspberry Pi 5, 8 GB | `rpi5` | 4 cores, aarch64 | 6 GB (hard limit), throttled from 5.5 GB | Qwen2.5 1.5B or Llama 3.2 1B, Q4_K_M |
| Jetson Orin Nano 8 GB / Orin NX | `jetson` | 6 cores, aarch64 | 6 GB, throttled from 5.5 GB | Llama 3.2 3B or Qwen2.5 1.5B, Q4_K_M |
| x86_64 mini PC, 16 GB (Intel N100, Ryzen) | `minipc` | 8 threads | 12 GB, throttled from 11 GB | Llama 3.1 8B or Llama 3.2 3B, Q4_K_M |

- A 64-bit Linux with systemd: Raspberry Pi OS 64-bit or Ubuntu 24.04 on the Pi, JetPack 6 on the
  Jetson, Debian 12 or Ubuntu 24.04 on the mini PC.
- The models as GGUF files (you download these yourself; they are not in the package).
- To build: a Linux x86_64 machine with `gcc`, `make`, `python3`, and for the two ARM boards the
  cross compiler (`sudo apt-get install gcc-aarch64-linux-gnu`). To run the built program on your
  PC before copying it to the board, also `sudo apt-get install qemu-user`.

For a 4 GB Pi 5, edit the profile before building: `MEMORY_MAX_MB=3072`, `MEMORY_HIGH_MB=2816`,
and one 1B-class model.

The Jetson's GPU is **not used**: zxvd runs its own integer inference engine on the CPU cores.
The Jetson still works as an edge node, but you are not getting its GPU speed.

## Build the package

From the top of the repository:

```sh
sh packages/edge/build_edge.sh rpi5   out/            # Raspberry Pi 5
sh packages/edge/build_edge.sh jetson out/            # Jetson
sh packages/edge/build_edge.sh minipc out/            # x86_64 mini PC
```

Add `--smoke` at the end to also run the built program once on your PC (through `qemu-aarch64`
for the ARM boards): it prints its version, starts as a daemon, answers one request on its
control socket, and stops.

Each build writes `out/zxv-edge-PROFILE-VERSION-ARCH.tar.gz` and a `.sha256` next to it. Inside:

```
opt/zxv-edge/bin/zxvd           the daemon (no window, stripped)
opt/zxv-edge/bin/libzxv.so      its library (found next to zxvd)
opt/zxv-edge/bin/zxv-edge-run   the launcher the service runs
opt/zxv-edge/models/            put your GGUF files here
opt/zxv-edge/install.sh         the installer
opt/zxv-edge/BUILDINFO          profile, architecture, compiler, source revision
opt/zxv-edge/MANIFEST.sha256    a hash of every file
etc/zxv-edge/edge.conf          the profile settings
lib/systemd/system/zxv-edge.service
```

The build uses the project's normal `build_zxvd.sh` without changes. It leaves out the AIPI
sandbox (an inference box does not need it; `EDGE_AIPI=1` puts it back) and links with
`-z separate-code`, so no part of the program is both writable and executable.

## Install on the board

Copy the tarball to the board, then:

```sh
mkdir /tmp/zxv-edge
tar -xzf zxv-edge-rpi5-*.tar.gz -C /tmp/zxv-edge
sudo /tmp/zxv-edge/opt/zxv-edge/install.sh
sudo cp ~/models/qwen2.5-1.5b-instruct-q4_k_m.gguf /opt/zxv-edge/models/
sudo chmod 0644 /opt/zxv-edge/models/*.gguf
sudo systemctl enable --now zxv-edge
journalctl -u zxv-edge -f
```

The installer checks every file against the manifest first. If you already have an
`/etc/zxv-edge/edge.conf`, it keeps yours and writes the new one as `edge.conf.new`. It does not
start anything: you start the service once the models are in place.

The steps are the same on all three boards; only the tarball name changes.

### The settings file

`/etc/zxv-edge/edge.conf` holds simple `KEY=VALUE` lines:

| Key | What it does |
|---|---|
| `MODELS` | GGUF file names in `MODELS_DIR`, best first. Today zxvd holds **one** model at a time, so the first file that exists is loaded and the others are only listed. |
| `MEMORY_MAX_MB`, `MEMORY_HIGH_MB`, `THREADS` | The memory wall, the point where the system starts slowing the service down, and the CPU share. These are written into the service file when the package is built; to change them on a board, use `sudo systemctl edit zxv-edge` and set `MemoryMax=`, `MemoryHigh=` or `CPUQuota=`. |
| `NET` | `off`, `lan` (the default here: only your local network) or `online`. |
| `NET_PORT`, `PEERS` | The UDP port for the peer network and any peers to contact at start (`IP:PORT`). |
| `PEER_SERVE_PRICE` | Empty means **off**, the default. A whole number turns on answering other people's nodes for that many VFV minor units per answer. Leave it off unless you mean to sell capacity. |
| `BOOKS` | `state` keeps the node's books in its private folder; `memory` forgets them at each restart. |

The launcher reads this file as data, never as a script: unknown keys, a key given twice,
quotes, `$`, backticks, `;`, pipes, `..` in paths and anything out of range are refused, and the
service does not start.

### How the service is locked down

The service runs as a throwaway user created at each start. The whole disk is read-only to it
except its own state and runtime folders. It cannot gain privileges, load kernel modules, change
the clock, see other users' processes or home folders, or open anything but local and internet
sockets. The control socket is readable only by that user and root, so use `sudo` to talk to it.
`systemd-analyze security` rates the unit at 1.2 out of 10 (lower is safer).

Two honest limits: zxvd sizes its own work from **all** the cores and memory it sees, not from the
limits above. The limits are hard walls set by systemd (the kernel stops the service at
`MemoryMax`), so a too-large model is killed rather than slowing the whole board.

## How nodes form a cluster

Turn on the peer network (`NET=lan`) on each node. Nodes find each other on the local network
through Vinea, the ZXV peer-to-peer layer, and talk over its post-quantum encrypted sessions.

1. **Adverts.** Every node regularly sends a short signed note, its *advert*: its node id, the
   models it has loaded and their quantisation, how fast it measured itself at reading a prompt
   and at writing an answer (tokens per second), its free memory, how many requests are waiting,
   and its price. The note is signed with the node's own post-quantum key (ML-DSA-87), and the
   node id is the hash of that key, so nobody can send an advert in someone else's name or change
   one in transit. Each advert has a lifetime (at most ten minutes); a node that stops sending
   them drops out of the cluster when its last one runs out.
2. **The token budget.** Each cycle the cluster has a fixed number of tokens it may generate. It
   is shared with the same Fibonacci rule the ZXV swarm uses inside one machine: the fastest node
   sits alone at the top level, the next two share the level below, the next three the one after,
   and so on, and each level's share follows the Fibonacci numbers.
3. **No monopoly.** No node may take more than a set share of a cycle (for example 40%). What it
   cannot take goes to the others in proportion to what they already have. If every node is at
   its limit, the rest is reported as unassigned rather than handed out. The shares always add up
   to exactly the budget.
4. **Who does what.** A request goes to a node that has the model, has the memory free, has
   tokens left in its share, and can meet the requested response time once its queue is counted.
   Among those, the quickest wins, then the cheaper, then a fixed order by node id.
5. **When a node fails.** Its unfinished requests move to other nodes, and its share of the
   budget goes back to them. Requests on healthy nodes stay where they are. The failed node
   rejoins when it sends a new advert after the failure.
6. **Everyone agrees.** The rules use whole numbers only and never depend on the order adverts
   arrived in, so every node that heard the same adverts reaches the same answer at the same time.

All of this is in `kernel/src/edgeclus/` (rules A1 to A9 in `edgeclus.h`).

## What was actually run, and what was not

Run in this repository on an x86_64 Linux container (2026-10-12):

- The x86_64 (`minipc`) package was built natively and its `zxvd` was started as a daemon; it
  answered `zxvd.version` on its control socket.
- The aarch64 (`rpi5` and `jetson`) packages were cross-built with `aarch64-linux-gnu-gcc` 13,
  and the aarch64 `zxvd` was run under `qemu-aarch64` user emulation: `--version`, then the daemon
  answering `zxvd.version` over its socket.
- The service file was checked with `systemd-analyze verify` (no warnings) and
  `systemd-analyze security` (exposure 1.2). The installer was run into a scratch folder.
- The cluster code passes its tests (`make -f Makefile -f mk/edge.mk test-edge`): advert signing
  and more than 20 tampered or malformed adverts, expiry at the exact millisecond, identical results under 119
  different arrival orders, the no-monopoly cap and exact budget conservation over 360 budget
  shapes, the scheduling rules, and failover. It also compiles freestanding for aarch64 and i386.

Not done or not tested:

- **Nothing has run on a real Raspberry Pi 5, Jetson or mini PC.** qemu user emulation is not the
  board: speed, thermal throttling (common on a fanless Pi 5), memory pressure and the real
  kernel are untested.
- The service has **not** been started under a real systemd here (the container has none), so the
  hardening settings have been checked for syntax and score, not for whether zxvd runs happily
  inside all of them. If a setting blocks something on a board, the journal will say which.
- No model was loaded in these runs, so no inference speed was measured.
- The cluster rules are not yet wired into `zxvd`: a node does not yet send adverts or take
  cluster work by itself. That needs changes to the daemon (its peer network and control socket),
  which is the next step.
- zxvd holds one model at a time, so a node advertises and serves one model until multi-model
  serving lands in the daemon.
- The speeds in an advert are what the node says about itself. A node that overstates them is
  only caught by missing its response times.
