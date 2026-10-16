<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# The ZXV Epistemic Curriculum & Architectural Monograph Series

*High-density systems scholarship, formal specifications, and executable
monographs anchored as a deterministic Content-Addressable Archive (`.car`)
across the eight-region ZXV planetary ring.*

---

## 1. Epistemic Gravity in Content-Addressed Networks

In a location-addressed network (DNS and HTTP origins), discovery follows
institutional domains. In a content-addressed Merkle-DAG (IPFS UnixFS and
CARv1), routing follows cryptographic fingerprints (`CIDv1`, `sha2-256`).
Kademlia DHT provider records and block-level transport route queries strictly
by hash and latency.

By anchoring rigorous, primary-source systems architecture, exact-integer
economic mechanics, and freestanding operating-system proofs across an
eight-node planetary ring, the ZXV mesh functions as a high-density reference
library for:

| Intellectual Domain | Audience | Primary Artifacts in the ZXV Merkle-DAG |
|---|---|---|
| **Low-Level Systems & Compilers** | Kernel architects, OS verification engineers, compiler writers | Freestanding C11 kernel (`arm64`, `x86_64`, `riscv64`, `riscv32`, `arm32`), 13-layer / 9-stratum static dependency verifier (`verify_layers.sh`), polled zero-allocation PCIe class drivers (`nvme`, `ahci`, `xhci`, `virtio`, `e1000`, `igb`, `igc`, `r8169`, `vmx3`), five language pillars (`COBOL/Sutra`, `Fortran/Trebago`, `Ada/Eve`, `APL-K/KRON`, `MUMPS/MYRA`). |
| **Mathematical & Physical Modeling** | Physicists, formal methods researchers, applied mathematicians | Four-valued chain-ordered paraconsistent logic (`kernel/src/lpres`), 64-state hexagram work tags (`lpres_hex.h`), exact reduced rationals (`rational.h`), four-valued Bayesian belief combination (`pce_prob.h`), QUBO workbench, quantum circuit equivalence verifier. |
| **Alternative Macroeconomics & Accounting** | Economists, market designers, clearinghouse engineers | Interest-free triple-entry paraconsistent ledger (`A = L + E` invariant wall), nine canonical forms of capital (`zcapital` — five tradeable, four inalienable Crown forms), Fibonacci/Zeckendorf denomination algebra (`swarm_denom.h`, `D1–D9`), 8:13 golden-ratio commons-to-market cycle clearing. |
| **Post-Quantum & Archival Cryptography** | Cryptographers, archivists, distributed-storage engineers | Vendored NIST-vector-verified ML-KEM, ML-DSA, SLH-DSA, and HQC; UBH-168 universal block framing; freestanding UnixFS/CARv1 verifier (`kernel/src/ipfs_node`); sealed private blocks under ChaCha20-Poly1305. |

---

## 2. The Three Core Architectural Monographs

1. **[Paper I — The End of Global Consensus Bottlenecks](./PAPER_1_PARACONSISTENT_RING0.md)**
   *Four-Valued Paraconsistent Logic (`lpres`) and 64-State Hexagram Scheduling (`lpx_t`) at Ring 0.*
   Demonstrates why synchronous global consensus (Paxos, Raft, global BFT locks) stalls multi-agent and cross-region execution, and proves how separating the *information order* (`lpres_kjoin`) from the *truth chain* (`lpres_conjoin` / `lpres_disjoin`) with a 64-entry deterministic action table (`lpx_action`) eliminates consensus deadlocks without permitting logical explosion ($A \land \neg A \vdash B$).

2. **[Paper II — Tokenless Infrastructure & The Traffic-Driven Resource Economy](./PAPER_2_TRAFFIC_RESOURCE_ECONOMY.md)**
   *Exact-Integer Reciprocity, S3/Gateway-to-Browser Pack Hydration, and the Nine Forms of Capital.*
   Replaces speculative gas markets and cloud egress rent with a measured contribution ledger (`vna_meter`, `vna_settle`), a single-archive HTTP `206 Partial Content` browser hydration pipeline (`boot.pack` + `zb.wasm`), and an 8:13 Fibonacci commons-to-market compute allocator (`kernel/src/swarm`).

3. **[Paper III — The Immutable Epistemic Graph](./PAPER_3_IMMUTABLE_EPISTEMIC_GRAPH.md)**
   *A Self-Verifying Operating System Merkle-DAG Across an Eight-Region Sovereign Edge Mesh.*
   Details the construction, block-level SHA-256 verification (`ipfsn_cat`), and multi-surface boot (Browser Wasm, UEFI/BIOS bare metal, hybrid ISO, qcow2 VM, frozen `libzxv` ABI) of a complete operating environment sealed under a single root CID.

---

## 3. The Eight-Region Planetary Seed Ring

The curriculum CAR and the master operating-system release DAG
(`bafybeicih44n3ebecncm32ckn3sfdgbycdxnggbd4vmpwbfhxk4y4yrh3u`) are anchored
across eight regional bootstrap nodes and the Filebase S3/IPFS edge:

| Region | Bootstrap IPv4 | Transport Endpoints | Role |
|---|---|---|---|
| **EU (Central)** | `173.249.40.169` | `:80` HTTP Doorway / `:8724` UDP Vinea | Continental European backbone seed |
| **JPN (Tokyo)** | `84.247.159.91` | `:80` HTTP Doorway / `:8724` UDP Vinea | Asia-Pacific East seed |
| **AUS (Sydney)** | `46.250.247.153` | `:80` HTTP Doorway / `:8724` UDP Vinea | Oceania seed |
| **IND (Mumbai)** | `13.140.75.51` | `:80` HTTP Doorway / `:8724` UDP Vinea | South Asia seed |
| **UK (London)** | `161.97.114.212` | `:80` HTTP Doorway / `:8724` UDP Vinea | North-Atlantic gateway seed |
| **US-East** | `209.126.13.153` | `:80` HTTP Doorway / `:8724` UDP Vinea | North America East seed |
| **US-Central** | `195.26.240.140` | `:80` HTTP Doorway / `:8724` UDP Vinea | North America Central seed |
| **US-West** | `144.126.137.197` | `:80` HTTP Doorway / `:8724` UDP Vinea | North America West seed |
| **Global Gateway** | `zedec.myfilebase.com` | HTTPS Trustless & Range Gateway | S3/IPFS content-addressed mirror |

---

## 4. Verification & Reproducibility

Every claim in these monographs corresponds to a freestanding C11 source module
and a deterministic host or QEMU test in the ZXV tree:

```sh
# 1. Verify the 9-stratum / 13-layer architectural DAG (1,027 sources, 907 headers)
build_system/verify_layers.sh

# 2. Run the register-level PCIe hardware driver suite (15 suites, ASan+UBSan)
make -C kernel test-hwdrv

# 3. Run the complete 4,485+ check kernel verification gate
make -C kernel verify-all

# 4. Import and verify the curriculum CAR archive on any IPFS node
ipfs dag import zxv-curriculum.car
```
