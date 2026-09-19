# ZEDEC XERO VULPINE (ZXV) Architecture

ZXV is a from-scratch, freestanding operating system kernel built on the M5 axiomatic substrate. It is not Linux, Unix, or DOS — it is a novel architecture designed for sovereign nodes and post-quantum security.

## Table of Contents

- [M5 Axiomatic Substrate](#m5-axiomatic-substrate)
- [Non-Binary Trit Logic](#non-binary-trit-logic)
- [Modbind Dynamic Electrical Coupling](#modbind-dynamic-electrical-coupling)
- [Phase-Tick Sequencing](#phase-tick-sequencing)
- [Boot Flow](#boot-flow)
- [Memory Management](#memory-management)
- [Interrupt Handling](#interrupt-handling)
- [Cryptographic Stack](#cryptographic-stack)
- [Filesystem](#filesystem)
- [Networking](#networking)

## M5 Axiomatic Substrate

The M5 axiomatic substrate replaces traditional kernel primitives with five core axioms:

| Axiom | Code | Purpose |
|-------|------|---------|
| **OSEQ** | K1 | Causal ordering / happens-before |
| **RMAG** | K2 | Exact Q32.32 arithmetic (no floating point) |
| **LPRES** | K3 | 4-valued logic (TRUE/FALSE/NEUTRAL/UNKNOWN) |
| **IPHASE** | K4 | Asymmetric routing |
| **CHOICE** | K5 | Deterministic collapse |

### OSEQ (K1) - Causal Ordering

Implements happens-before relationships using vector clocks. Events are totally ordered within a node and partially ordered across nodes.

**Location:** `kernel/src/oseq/`

**Key Functions:**
- `oseq_init()` - Initialize vector clock
- `oseq_event()` - Record an event
- `oseq_happens_before()` - Test ordering

### RMAG (K2) - Exact Arithmetic

Fixed-point arithmetic using Q32.32 format (32 integer bits, 32 fractional bits). No floating point, no rounding errors.

**Location:** `kernel/src/rmag/`

**Key Functions:**
- `rmag_add()` - Fixed-point addition
- `rmag_mul()` - Fixed-point multiplication
- `rmag_div()` - Fixed-point division
- `rmag_sqrt()` - Fixed-point square root

### LPRES (K3) - 4-Valued Logic

Replaces Boolean logic with 4-valued logic for paraconsistent reasoning.

**Values:**
- `LPRES_TRUE` - Definitely true
- `LPRES_FALSE` - Definitely false
- `LPRES_NEUTRAL` - Neither true nor false
- `LPRES_UNKNOWN` - Unknown state

**Location:** `kernel/src/lpres/`

**Key Functions:**
- `lpres_and()` - Logical AND
- `lpres_or()` - Logical OR
- `lpres_not()` - Logical NOT

### IPHASE (K4) - Asymmetric Routing

Routes events based on phase coordinates. Enables non-uniform event distribution.

**Location:** `kernel/src/iphase/`

**Key Functions:**
- `iphase_route()` - Route an event
- `iphase_register()` - Register a phase handler

### CHOICE (K5) - Deterministic Collapse

Collapses superposition states deterministically. Used in decision points.

**Location:** `kernel/src/choice/`

**Key Functions:**
- `choice_collapse()` - Collapse a superposition
- `choice_branch()` - Branch based on collapsed state

## Non-Binary Trit Logic

ZXV uses a 5-level trit lattice instead of Boolean logic:

```
FALSE < GLUT- < NEUTRAL < GLUT+ < TRUE
```

### Operators

**GLB (Greatest Lower Bound / Meet):**
```c
trit_t trit_glb(trit_t a, trit_t b);
```

**LUB (Least Upper Bound / Join):**
```c
trit_t trit_lub(trit_t a, trit_t b);
```

Both operators are:
- Associative: `(a ⊗ b) ⊗ c = a ⊗ (b ⊗ c)`
- Commutative: `a ⊗ b = b ⊗ a`
- Idempotent: `a ⊗ a = a`

**Location:** `kernel/src/trispace/`

## Modbind Dynamic Electrical Coupling

Module resolution evaluates electrical power factors before linking:

```
P = V · I · pf(Δφ)
```

Where:
- `P` = Power
- `V` = Voltage
- `I` = Current
- `pf(Δφ)` = Power factor based on phase difference

This prevents:
- Dead code linking (unreferenced modules are discarded)
- Interface mismatches (structural congruence verified at link time)
- Silent failures (mismatched modules fail to link)

**Location:** `kernel/src/modbind/`

**Key Functions:**
- `modbind_compose()` - Compose modules
- `modbind_verify()` - Verify module graph
- `modbind_declare()` - Declare a module

## Phase-Tick Sequencing

ZXV does not use a global clock. Execution is driven by phase-tick events from the phase coordinator.

### Phase Coordinator

Coordinates execution across subsystems using phase ticks.

**Location:** `kernel/src/phase_coord/`

**Key Functions:**
- `phase_coord_init()` - Initialize coordinator
- `phase_coord_tick()` - Emit a phase tick
- `phase_coord_register()` - Register a phase handler

### Event Loop

Main event loop processes phase ticks and dispatches to handlers.

**Location:** `kernel/src/event_space/`

**Key Functions:**
- `event_loop()` - Main event loop
- `event_dispatch()` - Dispatch an event

## Boot Flow

### ARM64 Boot Sequence

1. **EL3 (Secure Monitor)** - Optional, for ARM SMC calls
2. **EL2 (Hypervisor)** - Optional, for virtualization
3. **EL1 (Kernel)** - Main kernel execution
4. **EL0 (Userspace)** - User processes

**Boot Stages:**

1. `boot.s` - Assembly entry point
   - Set up stack
   - Enable MMU
   - Jump to C code

2. `kernel_main_arm64.c` - C entry point
   - Initialize GIC interrupt controller
   - Initialize generic timer
   - Initialize M5 subsystems
   - Start event loop

**Location:** `kernel/arch/arm64/`

### x86_64 Boot Sequence

1. **Multiboot2** - GRUB loads kernel
2. **Protected Mode** - Enter 64-bit long mode
3. **Kernel** - Initialize subsystems

**Location:** `kernel/arch/x86_64/`

### RISC-V Boot Sequence

1. **OpenSBI** - Firmware initializes hardware
2. **M-Mode** - Machine mode (firmware)
3. **S-Mode** - Supervisor mode (kernel)
4. **U-Mode** - User mode (processes)

**Location:** `kernel/arch/riscv/`

## Memory Management

### Physical Memory

- Identity mapping for kernel code/data
- Dynamic allocation via `mm_alloc()`
- Page-based allocation (4KB pages)

**Location:** `kernel/src/mm/`

**Key Functions:**
- `mm_init()` - Initialize memory manager
- `mm_alloc()` - Allocate physical pages
- `mm_free()` - Free physical pages

### Virtual Memory

- Page tables for address translation
- Per-process address spaces
- Copy-on-write for fork

**Location:** `kernel/arch/<arch>/mm.c`

## Interrupt Handling

### ARM64 (GICv3)

- GIC distributor routes interrupts
- GIC redistributor per-core
- IRQ/FIQ handling

**Location:** `kernel/arch/arm64/gicv3.c`

### x86_64 (IDT/PIC)

- Interrupt Descriptor Table
- 8259 PIC or APIC
- IRQ handling

**Location:** `kernel/src/idt/`, `kernel/src/pic/`

## Cryptographic Stack

### Post-Quantum

- **ML-KEM-768** - Key encapsulation mechanism (FIPS 203)
- **ML-DSA-65** - Digital signatures (FIPS 204)

**Location:** `kernel/src/mlkem/`

### Classical

- **X25519** - Diffie-Hellman key exchange
- **Ed25519** - Digital signatures
- **ChaCha20-Poly1305** - AEAD encryption
- **AES-256-GCM** - AEAD encryption
- **SHA-256/SHA-3** - Hash functions

**Location:** `kernel/src/robin_debanks/`, `kernel/src/tls/`

### Custom

- **CURZI-8889-A** - 8889-bit lattice sponge
- **CURZI-8889-A-SEAL** - Post-quantum AEAD
- **CURZI-8889-A-SIGN** - Lattice ring signatures

**Location:** `kernel/src/curzi/`

## Filesystem

### ZXVFS (Tri-Space Filesystem)

Native filesystem with tri-space addressing:
- `.zxvc` - Content-addressed files
- `.cedez` - Encrypted data
- `.cedec` - Encrypted content

**Location:** `kernel/src/zxvfs/`

### FAT32

Read/write support for FAT32 partitions.

**Location:** `kernel/src/fat32/`

## Networking

### Stack

- **TCP/UDP** - Transport layer
- **IP** - Network layer
- **Ethernet** - Data link layer
- **Virtio-net** - Virtual network device

**Location:** `kernel/src/net/`

### Protocols

- **DHCP** - Dynamic host configuration
- **DNS** - Domain name resolution
- **HTTP** - Web protocol (basic)

## Further Reading

- `SOURCE_CODE_NAVIGATION_GUIDE.md` - Source code organization
- `SUBSYSTEM_INDEX.md` - Subsystem catalog
- `VOVINA_SHAKINA_WHITE_PAPER.md` - Deep technical paper
