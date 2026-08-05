# CELL-001: Cellular Multikernel Evidence

## What was implemented

1. **Cell contract and control plane**
   - `kernel/src/cellular_multikernel/cellular_multikernel.h`
   - `kernel/src/cellular_multikernel/cellular_multikernel.c`
   - Cell identity, architecture, bitness, trust domain, privilege, incarnation,
     capabilities, supported schemas, memory ownership, transport endpoints,
     resource budget, health, and lifecycle states.
   - Control plane: discover, authenticate, quarantine, admit, activate, revoke,
     fault handling, and phase routing.

2. **Data plane**
   - Bounded per-endpoint event queues.
   - Backpressure (send fails when full; receive fails when empty).
   - Transport endpoint model for cache-coherent ring, non-coherent queue,
     mailbox, PCIe/CXL, NoC, and network.

3. **Tri-space commit barrier**
   - `triad_commit_state_t` tracks S+, S-, S0 lanes.
   - `triad_commit_update` enforces: one vote per lane, explicit reject,
     commit token only after S+, S-, and S0 are resolved and OK.

4. **CRIT-168 canonical value**
   - 21-octet canonical representation.
   - Roundtrip equality and digest folding (production will use cryptographic
     digest; value semantics are independent of hash).

5. **Host evidence**
   - `tests/host/test_cellular_multikernel.c`: 9/9 tests passing.
   - Included in `make verify-all`.

6. **ARM64 boot integration**
   - `kernel/arch/arm64/kernel_main_arm64.c` calls `cell_fabric_boot_init()`.
   - Three logical ARM64 cells are discovered, authenticated, admitted, and
     activated: `arm64-coord`, `arm64-k1k6`, `arm64-io`.
   - Phase routes are installed for OSEQ (coord), RMAG/LPRES/CHOICE (k1k6),
     and network/audio (io).
   - A fault is injected into `arm64-io`; the cell is revoked and its routes
     are removed, demonstrating containment.
   - The cell is re-admitted with a new incarnation and new routes.

## Evidence from QEMU

```text
[E0083] [BOOT] Cellular Multikernel (CELL-001): fabric discovery and admission...
[E0084]   [INITIALIZED] 3 ARM64 cells active (coord, k1k6, io) with phase routes
[E0085]   [FAULT CONTAINED] arm64-io fail-stopped; routes revoked; fabric survives
[E0086]   [RECOVERED] arm64-io re-admitted with incarnation=2 and new routes
...
[E0087] [BOOT_OK] Phase E0082 complete; kernel_main reached; Stage-1 kernel loop ready
[BOOT_OK] heartbeat: cycle=100
```

## Maturity gate status

- **CELL0 specification**: `cellular_multikernel.h` frozen; transport ABI and
  fault model in code and tests.
- **CELL1 emulation**: Multi-cell QEMU boot demonstrated with 3 logical cells,
   panic isolation, and deterministic event trace.  Physical multi-VM cells
   remain future work.
- **CELL2 multicore / CELL3 heterogeneous / CELL4 production**: Not yet reached.

## Remaining work

- Implement multi-VM / multi-core cell boot in QEMU with independent memory.
- Add IOMMU/DMA containment, signed cell images, and LPRES attestation.
- Build x86-64, RISC-V, and DSP cell images for the universal bundle.
- Measure scaling, jitter, energy, and recovery curves.
