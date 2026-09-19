# M5 Axiomatic Kernel SDK — Developer Guide

## Overview

The M5 SDK provides a complete development environment for building user space applications on the M5 Axiomatic Kernel. Applications run at EL0 (ARM64) with full access to the kernel's axiomatic services through the `m5_api.h` interface.

## Quick Start

### 1. Create Your Application

```bash
# Copy the template
cp kernel/src/sdk/app_template.c myapp.c

# Edit your application
vim myapp.c
```

### 2. Build

```bash
# Build for ARM64 (default)
make -f kernel/src/sdk/Makefile.app APP=myapp

# Build for x86_64
make -f kernel/src/sdk/Makefile.app APP=myapp ARCH=x86_64

# Build for RISC-V
make -f kernel/src/sdk/Makefile.app APP=myapp ARCH=riscv64
```

### 3. Test on Host

```bash
# Compile and run on host (Linux/macOS)
make -f kernel/src/sdk/Makefile.app APP=myapp test
```

### 4. Deploy to Kernel

Applications must be linked into the kernel image. Add your app to `kernel/src/apps/` and rebuild the kernel:

```bash
# Add to kernel build
cp myapp.c ../../kernel/src/apps/
# Edit kernel/src/apps/apps.c to include your app
make -C kernel -f build_system/Makefile.arm64 all
```

## API Reference

### Core Axiomatic Services

| Module | Header | Description |
|--------|--------|-------------|
| **OSEQ** | `m5_oseq_*` | Causal ordering, happens-before |
| **RMAG** | `m5_rat_*` | Exact rational arithmetic (no FP) |
| **LPRES** | `m5_lpres_*` | Paraconsistent 4-valued logic |
| **IPHASE** | `m5_iphase_*` | Asymmetric routing, complex phase |
| **CHOICE** | `m5_choice_*` | Deterministic collapse |
| **Phase Coordinator** | `m5_pc_*` | Admission gates, coverage |

### Financial Services

| Module | Header | Description |
|--------|--------|-------------|
| **Triple Ledger** | `m5_ledger_*` | Three-rail settlement (846/888/999) |
| **Vouchers** | `m5_voucher_*` | Merit-based, no-debt instruments |
| **Derivatives** | `m5_deriv_*` | Kernel-enforced 100% backing |
| **Assurance** | `m5_assurance_*` | Pay-It-Forward capital generation |
| **Treaty Tokenization** | `m5_treaty_*` | Conservation easement tokens |

### Capital Forms (Nine Forms)

```c
typedef enum {
    M5_FORM_SOCIAL = 0,              // State-Reserved
    M5_FORM_NATURAL = 1,             // State-Reserved
    M5_FORM_HERITAGE_INTELLECTUAL = 2, // State-Reserved
    M5_FORM_GOVERNANCE_INSTITUTIONAL = 3, // State-Reserved
    M5_FORM_FINANCIAL = 4,           // Priceable
    M5_FORM_MATERIAL = 5,            // Priceable
    M5_FORM_LIVING = 6,              // Priceable
    M5_FORM_KNOWLEDGE = 7,           // Priceable
    M5_FORM_BUILT = 8                // Priceable
} m5_capital_form_t;
```

### Smart Contracts (ZAB VM)

```c
// Register a ZAB bytecode contract
int32_t m5_vena_register_contract(const char *name, const char *code,
                                  m5_vena_language_t lang, const char *creator);

// Execute a contract
int32_t m5_vena_execute_contract(uint32_t contract_id, const char *args,
                                 char *result, uint32_t max_result);
```

### Apps Framework

```c
// Load and start apps
int32_t m5_vena_load_app(const char *name, m5_app_type_t type,
                         const char *code, m5_vena_language_t lang);
int32_t m5_vena_start_app(uint32_t app_id);
```

### System Calls

```c
int64_t m5_syscall(m5_syscall_t num, uint64_t arg0, ...);
```

| Syscall | Number | Args |
|---------|--------|------|
| `exit` | 0 | code |
| `getpid` | 1 | - |
| `yield` | 2 | - |
| `write` | 3 | char |
| `read` | 4 | - |
| `exec` | 5 | cmd_ptr, len |
| `sleep` | 6 | ms |
| `send` | 7 | dest_pid, msg_ptr, len |
| `recv` | 8 | sender_pid, buf_ptr, max_len |
| `open` | 9 | path_ptr, flags |
| `close` | 10 | fd |

### IPC

```c
int32_t m5_ipc_send(uint32_t dest_pid, const uint8_t *payload, uint32_t length);
int32_t m5_ipc_recv(uint32_t sender_pid, uint8_t *buffer, uint32_t max_len);
```

### VFS

```c
int32_t m5_vfs_list_dir(const char *path, m5_vfs_node_t *entries, uint32_t max);
int32_t m5_vfs_read_file(const char *path, uint8_t *buf, uint32_t max, uint32_t *out_len);
int32_t m5_vfs_write_file(const char *path, const uint8_t *buf, uint32_t len);
```

### Network / Mesh

```c
int32_t m5_net_list_ifaces(m5_net_iface_t *ifaces, uint32_t max);
int32_t m5_mesh_route_price(const m5_cid_t dest_cid, m5_rat_t *out_price);
```

### GUI

```c
void m5_gui_write(uint32_t window, const char *text);
void m5_gui_write_attr(uint32_t window, const char *text, m5_attr_t attr);
void m5_gui_newline(uint32_t window);
void m5_gui_clear(uint32_t window);
```

## Self-Audit

Every application should implement self-audit:

```c
m5_audit_result_t myapp_self_audit(void) {
    // Check invariants
    if (my_invariant_violated()) return M5_AUDIT_FAIL;
    return M5_AUDIT_PASS;
}
```

The kernel calls `m5_self_audit()` at boot and every 1000 ticks.

## Best Practices

1. **No Floating Point** — Use `m5_rat_t` for all arithmetic
2. **No malloc** — Use stack allocation or static buffers
3. **Phase Ticks, Not Wall Clock** — Use `m5_pc_current_tick()` for timing
3. **LPRES for Uncertainty** — Return `M5_NEITHER` for unknown, not `false`
4. **Exact Rational** — `m5_rat_make(1, 10)` not `0.1`
5. **Fail Closed** — Unbound hooks = immediate veto
6. **Coverage Gate** — `r·ℓ ≥ 1.8` enforced at kernel level

## Building for Production

```bash
# 1. Build kernel with your app
make -C kernel -f build_system/Makefile.arm64 all

# 2. Create bootable image
make -C kernel -f build_system/Makefile.arm64 run

# 3. Deploy to hardware
# Copy kernel_arm64.elf to target device
```

## Debugging

```bash
# Host test with sanitizers
make -f kernel/src/sdk/Makefile.app APP=myapp test

# QEMU with GDB
qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
    -kernel kernel_arm64.elf -s -S -nographic -serial mon:stdio
# In another terminal: gdb-multiarch kernel_arm64.elf
```

## Language Support

The kernel supports 32 languages natively, including:
- English, Chinese, Japanese, Korean, Arabic, Hebrew
- Hindi, Sanskrit, Russian, French, German, Spanish, Portuguese
- Swahili, Amharic, Navajo, Inuktitut, Basque, Welsh, Gaelic
- Klingon, Quenya, Dothraki, Esperanto, Lojban, Ithkuil, Toki Pona
- Sign Language, Braille, Morse, Binary, M5 Axiomatic

## License

All code in this SDK is licensed under:
- OPL-1.1 (Open Piracy License)
- CC BY-SA 4.0
- Royal Writ of the Sicilian Crown 1.0
- SEL-3.3

See `LICENSE` at repository root.

## Support

- **Architect**: Midnight Miqu 70B — `http://216.243.220.53:8000/v1/chat/completions`
- **Coder**: Qwen 2.5 Coder 32B — `http://216.243.220.53:8001/v1/chat/completions`
- **Email**: deal@zedec.ai
- **Reference**: M5-SDK-2026-001
