# libzxv-econ

The ZXV engine facade as one static library and one header, `zxv_econ.h`.

| Call | What it does |
|---|---|
| `zxv_compress` | Multilateral netting of gross obligations (the abacus): every member's net position is kept exactly, cycles cancel, at most members-1 legs remain. |
| `zxv_route` | Cheapest path over directed corridors, each with its own fee (parts per million) and capacity, so the cost A→B need not equal B→A. Exact integers; ties go to fewer hops, then the lowest member id. |
| `zxv_decompress` | Posts the legs to the ledger of record (`pay_ledger`), each as one atomic payment with the 0.08889% assurance fee split into its four buckets. All or nothing: a refusal part-way rolls back what already posted. |

Lifecycle: `zxv_engine_init`, `zxv_engine_add_member`, `zxv_engine_fund`,
`zxv_engine_withdraw`, `zxv_engine_add_corridor`, `zxv_engine_check`.

## Build

```sh
make -C packages econ          # build/libzxv-econ/libzxv-econ.a + include/
make -C packages check-econ    # symbol check + smoke program
cc -Ipackages/build/libzxv-econ/include app.c packages/build/libzxv-econ/libzxv-econ.a
```

Compiled in place from `kernel/src/engine`, `abacus`, `rational`, `pay`
(ledger, util, assure), `swarm` (the capped split the fee uses), `tensor/zt.c`
and `mlkem/keccak.c`. Tested by `make -C kernel test-engine`.

## Properties

- No allocation, no floating point, no randomness source: a 32-byte seed
  derives every posting id, so the same calls give the same hash chain on
  every machine.
- `zxv_engine_t` holds the ledger journal and is large: keep it static or on
  the heap.
- Undefined symbols: only `memcpy`/`memset`/`memmove`/`memcmp` and libgcc's
  128-bit division helpers (the rationals use `__int128`), so it is for
  64-bit targets.

## Honest limits

Netting is not consensus, and a route is not liquidity: corridor capacities
are what the caller declares. Operating this for other people's money needs
licences; nothing here is certified.
