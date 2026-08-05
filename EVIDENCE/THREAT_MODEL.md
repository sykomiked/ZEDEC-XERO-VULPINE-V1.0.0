# Threat Model
## Vovina Shakina M5 Axiomatic Kernel — ZEDEC pqOS
### Date: 2026-08-03 | Classification: Internal

## Trust Boundaries
1. **EL1 (Kernel) ↔ EL0 (User)**: Hardware-enforced via ARM64 exception levels
2. **Kernel ↔ Network**: All packets parsed in kernel, M5 metadata verified
3. **Kernel ↔ Storage**: FAT32 on RAM disk (dev), signed updates via Ed25519
4. **Process ↔ Process**: IPC message queues, per-process page tables (TTBR0)

## Threat Surface

### T1: Unauthorized Code Execution
- **Risk**: Medium
- **Vector**: Malformed ELF loaded into EL0, buffer overflow in kernel
- **Mitigation**: Per-process page tables (TTBR0_EL1), UXN/PXN bits, stack canary disabled (freestanding), Ed25519 signature verification for updates
- **Status**: Page table isolation active. No ASLR yet. Stack canary disabled for freestanding.

### T2: Network Packet Injection
- **Risk**: Medium
- **Vector**: Malformed Ethernet/IP/TCP packets, ARP spoofing
- **Mitigation**: Packet length validation, protocol field checks, M5 axiomatic header verification (magic 0x4D354158)
- **Status**: Basic validation active. No firewall rules beyond port-seal. No rate limiting.

### T3: IPC Message Spoofing
- **Risk**: Low
- **Vector**: Process sends message claiming to be from another PID
- **Mitigation**: sender_pid set by kernel (proc_current()->pid), not by sender
- **Status**: Mitigated. Kernel controls sender_pid.

### T4: Filesystem Corruption
- **Risk**: Low (RAM disk, dev only)
- **Vector**: Malformed FAT32 BPB, directory entry overflow
- **Mitigation**: BPB validation (bytes_per_sector != 0), cluster chain bounds check
- **Status**: Basic validation active. No journaling. RAM disk is volatile.

### T5: Side-Channel (Spectre/Meltdown)
- **Risk**: Low (QEMU emulation, no speculative execution)
- **Vector**: Cache timing, branch prediction
- **Mitigation**: Not applicable in QEMU. For real hardware: ARM64 CSV2/CSV3, kernel page table isolation
- **Status**: Not mitigated (QEMU only). Real hardware would need CSV2/CSV3 enablement.

### T6: Denial of Service
- **Risk**: Low
- **Vector**: Process exhaustion, memory exhaustion, infinite loops
- **Mitigation**: MAX_USER_PROCS=32, quantum-based preemption, 2MB user page pool
- **Status**: Mitigated. Preemptive scheduler prevents CPU monopolization.

### T7: Cryptographic Weakness
- **Risk**: Critical (if real keys used)
- **Vector**: Weak RNG, side-channel in Ed25519, AES-GCM nonce reuse
- **Mitigation**: Hardware entropy (CNTVCT_EL0 + CNTFRQ_EL0), RFC 8032 Ed25519, unique per-boot vault key
- **Status**: Hardware entropy source active. Ed25519 uses RFC 8032 constant-time field arithmetic. AES-GCM key derived per-boot.

## Security Controls Matrix

| Control | Status | Notes |
|---------|--------|-------|
| Memory isolation (MMU) | ACTIVE | TTBR0/TTBR1 split, per-process page tables |
| Execute-never (UXN/PXN) | ACTIVE | Set on stack pages, configurable per mapping |
| Stack protection | DISABLED | Freestanding (-fno-stack-protector) |
| ASLR | NOT IMPL | Fixed user code at 0x10000, stack at 0x80000 |
| Code signing | ACTIVE | Ed25519 signature verification for updates |
| Secure boot | ACTIVE | Porter House boot evidence chain |
| Network firewall | PARTIAL | Port-seal (8800, 9090, 23) |
| Audit logging | ACTIVE | Boot evidence measurements, event-space self-audit |
| Secure key storage | ACTIVE | Per-boot vault key from hardware entropy |
| Constant-time crypto | ACTIVE | Ed25519 field arithmetic, SHA-256 |

## Recommendations
1. **ASLR**: Randomize user code/stack base addresses
2. **Stack canary**: Enable for EL0 processes (not kernel)
3. **Network firewall**: Add configurable iptables-style rules
4. **Persistent storage encryption**: AES-GCM for on-disk data
5. **CSV2/CSV3**: Enable on real ARM64 hardware for Spectre mitigation
6. **Formal verification**: TLA+ model of scheduler and IPC for safety properties
