<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# zephyr-zxv: the ZXV Zephyr module

This is a Zephyr module (`zephyr/module.yml`, `Kconfig`, `CMakeLists.txt`).
It builds four things into one library, `zxv`:

- the libzxv microcontroller subset, straight from `sdk/libzxv/src` and
  `kernel/src`, with nothing copied;
- the port layer, `../port`, with `zxp_zephyr.c`;
- the `zxe` ledger;
- the phase-clock driver.

The sample [`samples/ledger_budget`](samples/ledger_budget) exercises the
following on emulators:

- fail-closed start-up;
- the ledger;
- a posting budget;
- the phase clock.

**Advisory only.** Nothing here actuates. Read
[../README.md, "Safety"](../README.md#safety-advisory-only).

## Add it to your application

With west, add the module to your manifest. Point `path` at `sdk/embedded/zephyr-zxv` of a
ZEDEC-XERO-VULPINE checkout. Or pass it directly:

```sh
west build -b <board> app -- -DEXTRA_ZEPHYR_MODULES=/path/to/sdk/embedded/zephyr-zxv
```

The module needs the rest of the repository next to it (`../port`, `../zxe`,
`../../libzxv`, `../../../kernel`). It is not a standalone copy.

Then in `prj.conf`:

```
CONFIG_ZXV=y
CONFIG_ZXV_HEAP_SIZE=98304          # dedicated k_heap; never the system heap
CONFIG_ZXV_ENTROPY_CSPRNG=y         # needs a real entropy driver (zephyr,entropy)
```

## Kconfig

| option | default | meaning |
|---|---|---|
| `CONFIG_ZXV` | n | build the module |
| `CONFIG_ZXV_HEAP_SIZE` | 98304 | bytes in ZXV's own `k_heap`. An allocation that does not fit fails with `ZXV_E_NOMEM`. |
| `CONFIG_ZXV_ENTROPY_NONE` | **default** | nothing is installed. Your code calls `zxp_set_entropy()` with the board TRNG, then `zxp_init()`. Until then, ZXV is closed. |
| `CONFIG_ZXV_ENTROPY_CSPRNG` | | uses `sys_csrand_get()`. Offered only when `ENTROPY_GENERATOR` and `CSPRNG_ENABLED` are set and Zephyr is **not** falling back to `TEST_CSPRNG_GENERATOR`. |
| `CONFIG_ZXV_ENTROPY_INSECURE_TEST` | | `sys_rand_get()` with `TEST_RANDOM_GENERATOR`, which is **not random**. For QEMU and `native_sim` only. Prints a warning at boot. Never ship it. |
| `CONFIG_ZXV_AUTO_INIT` | y | installs the chosen source and runs `zxp_init()` at `APPLICATION` init. If that fails, the system still boots and ZXV stays closed (`zxv_ctx_create()` returns `ZXV_E_INTERNAL`). |
| `CONFIG_ZXV_PQC` | n | adds ML-KEM-1024, ML-DSA-87 and Ed25519. ML-DSA-87 signing needs about 121 KB of stack, so this is for gateways only. |

**Threads.** With `CONFIG_THREAD_LOCAL_STORAGE=y`, libzxv keeps its per-thread
error text and is thread-safe. TLS needs the Zephyr SDK toolchain; the GNU
Arm Embedded toolchain does not support it. Without TLS, the module builds
with `ZXP_NO_TLS`: one owner thread must make every ZXV call, and a call from
another thread is fatal (`zxp_fatal`, then `k_panic`). The sample uses one
thread.

**Memory.** On a 32-bit core, the heap needed is:

- context: 1.3 KB
- rmag: 35 KB
- phase clock: 22 KB, plus growth (see `../README.md`, "RAM")

The `zxe_ledger` lives in your own static arrays.

## The sample

`samples/ledger_budget` does the following:

- Checks that ZXV is closed without entropy.
- Opens a context and capabilities.
- Posts through `zxe_ledger`, including the refusals for insufficient funds
  and a duplicate reference.
- Spends and refills an rmag budget.
- Advances the phase clock from `k_uptime` with `zxe_clock`.
- Prints the heap statistics.

The sample tolerates `ZXV_E_NOMEM` for the phase clock on small parts. The
twister console harness expects `zxv sample: N checks, 0 failures`.

### Built and run here

These builds used Zephyr **v4.2.2** with plain `cmake` + `ninja`; `west` was
not installed. `$ZEPHYR` is a Zephyr v4.2.2 checkout.

`native_sim/native/64` (host toolchain):

```sh
export ZEPHYR_BASE=$ZEPHYR ZEPHYR_TOOLCHAIN_VARIANT=host
cmake -S samples/ledger_budget -B build/native -GNinja -DBOARD=native_sim/native/64 \
      -DZEPHYR_MODULES=$PWD
ninja -C build/native && build/native/zephyr/zephyr.exe -stop_at=5
```

Output:

```
zxp: WARNING: INSECURE test entropy (CONFIG_ZXV_ENTROPY_INSECURE_TEST); never ship this configuration
zxv sample: phase tick 9147, ZXV heap live 58824 bytes, peak 58824
zxv sample: 30 checks, 0 failures
```

`qemu_cortex_m3` (LM3S6965, 64 KB RAM). This used the GNU Arm Embedded
toolchain from apt, plus Zephyr's `cmsis_6` module at the revision pinned in
Zephyr's `west.yml`, `06d952b6713a2ca41c9224a62075e4059402a151`:

```sh
export ZEPHYR_BASE=$ZEPHYR ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=/usr
cmake -S samples/ledger_budget -B build/m3 -GNinja -DBOARD=qemu_cortex_m3 \
      "-DZEPHYR_MODULES=$PWD;/path/to/cmsis_6"
ninja -C build/m3       # FLASH 29,828 B / 256 KB, RAM 49,848 B / 64 KB
qemu-system-arm -M lm3s6965evb -nographic -kernel build/m3/zephyr/zephyr.elf
```

Output:

```
zxv sample: phase clock refused (ZXV_E_NOMEM): CONFIG_ZXV_HEAP_SIZE=40960
zxv sample: phase tick 0, ZXV heap live 36360 bytes, peak 36360
zxv sample: 24 checks, 0 failures
```

`boards/qemu_cortex_m3.conf` lowers the heap to 40 KB so that the image fits
in 64 KB. A context, a capability, an rmag and the ledger fit; a phase clock
does not.

With west and twister, the usual commands are:

```sh
west build -b qemu_cortex_m3 samples/ledger_budget -- -DEXTRA_ZEPHYR_MODULES=$PWD
west twister -T samples -p qemu_cortex_m3 -p native_sim/native/64 \
     -x=EXTRA_ZEPHYR_MODULES=$PWD
```

The CI workflow (`.github/workflows/sdk-embedded.yml`) runs them with the
Zephyr SDK (`west sdk install`), and also builds with
`CONFIG_THREAD_LOCAL_STORAGE=y`. They were **not** run locally. On
`native_sim`, Kconfig refuses `CONFIG_THREAD_LOCAL_STORAGE`; this was
tried.

## Not done

- The module has not been built with the Zephyr SDK toolchain locally, so
  `CONFIG_THREAD_LOCAL_STORAGE=y` is untested.
- It has not run on a real board, and `CONFIG_ZXV_ENTROPY_CSPRNG` has not
  been tried against a hardware entropy driver.
- No timing has been measured.
