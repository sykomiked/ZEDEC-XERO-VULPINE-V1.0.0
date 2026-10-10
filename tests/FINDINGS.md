# Tiered-test findings (bugs the new tests expose)

Every entry here is a real defect in module code that a Tier 1/2 test
demonstrates. The test is kept and listed in its file's `KNOWN_FAILURES`
table, so it is reported as `[XFAIL]` on every run (never silently passed).
When a fix lands, the check turns into `[XPASS]`: delete the entry from the
table and from this file. `TIER_STRICT=1` makes an `[XPASS]` fail the run
(the mutation harness sets it, so a mutant that changes a known bug's
behaviour is counted as killed).

The patches below are proposals for the coordinator: the module sources were
being edited by other workers when these tests were written, so none were
applied here.

| id | where | test |
|----|-------|------|
| F-RMAG-DIV0 | kernel/src/rmag/rmag_core.c `rmag_div_quotas` | tests/01_root_axioms/axioms_rational.c |
| F-RMAG-OVF | kernel/src/rmag/rmag_core.c all four `rmag_*_quotas` | axioms_rational.c |
| F-RMAG-REINIT | kernel/src/rmag/rmag_core.c `rmag_init` (via `vino_init`) | tests/02_integration/integ_escrow.c |
| F-PAY-NULL | kernel/src/pay/pay_ledger.c, pay_tithe.c | axioms_pay.c |
| F-TL-NULL | kernel/src/finance/triple_ledger.c | axioms_triple.c |
| F-TL-COVERAGE | kernel/src/finance/triple_ledger.c `triple_ledger_verify_coverage` | axioms_triple.c |
| F-VINO-ADDR | kernel/src/vino/vino.c `vino_create_account` | axioms_chest.c |
| F-VINO-BALCAP | kernel/src/vino/vino.c `vino_get_balance` | axioms_chest.c |
| F-VINO-NULL | kernel/src/vino/vino.c `vino_get_account` | axioms_chest.c |
| F-CH-WRAP | kernel/src/count_house/count_house.c `count_house_deposit` | axioms_chest.c |
| F-PH-FULL | kernel/src/porter_house/porter_house.c seal table | axioms_chest.c |
| F-CC-REJECTED | kernel/src/community_chest/community_chest.c purchases | axioms_chest.c |
| F-CC-CASHIN-UNJOURNALED | community_chest.c `cc_voucher_cash_in/out` | integ_escrow.c |
| F-FS-ALIGN | kernel/include/freestanding.h `fs_malloc` | axioms_alloc.c |
| F-FS-PER-TU | kernel/include/freestanding.h `fs_malloc` | axioms_alloc.c |
| F-MM-DFREE | kernel/src/mm/mm.c `kfree` | axioms_alloc.c |
| F-MM-FRAME0 | kernel/src/mm/mm.c frame 0 as "no frame" | axioms_alloc.c |
| F-MM-QUOTA | kernel/src/mm/mm.c `mm_free_frame` | axioms_alloc.c |
| F-MM-ALIGN | kernel/src/mm/mm.c `kmalloc` size rounding | axioms_alloc.c |
| F-MM-SHIFT31 | kernel/src/mm/mm.c bitmap helpers | axioms_alloc.c (sanitizer build) |

## Exact rationals (rmag_core)

**F-RMAG-DIV0.** `rmag_div_quotas(a, {0,1})` builds `{a.num*1, a.den*0}` and
`rational_normalize` turns the zero denominator into an ordinary finite value
(`-3/1 / 0` comes back as `-1/1`). A division by zero must fail closed.
Patch: return the invalid quota `{0, 0}` when `b.num == 0` (or either
denominator is 0), and have callers treat `den == 0` as invalid:

```c
rational_t rmag_div_quotas(rational_t a, rational_t b) {
    if (b.num == 0 || a.den == 0 || b.den == 0) return (rational_t){0, 0};
    ...
```

**F-RMAG-OVF.** `rmag_add/sub/mul/div_quotas` multiply `int64_t` operands
with no overflow check (`INT64_MAX/1 + 1/1` and `2^62 * 4` are signed
overflow, UB; UBSan aborts on them). Patch: use checked arithmetic as
kernel/src/rational does (`__builtin_mul_overflow` / `__builtin_add_overflow`
on the cross products, reducing by the gcd first) and return `{0, 0}` on
overflow.

**F-RMAG-REINIT.** `rmag_init` returns at once when a table already exists,
so `vino_init` on a NEW ledger keeps the previous ledger's quota mirror (and a
later `rmag_init(n)` with a larger `n` is silently ignored). Patch: when a
table exists, reset its slots to `0/1` (growing it if `slot_count` is
larger), or give `vino_ledger_t` its own quota table instead of the global.

## pay

**F-PAY-NULL.** `pay_ledger_verify_chain(NULL)`, `pay_ledger_totals(NULL, ...)`
and `pay_commons_conserved(NULL)` dereference NULL (every other pay entry point
refuses NULL). Patch: `if (!L) return false;` / zero the outputs and return /
`if (!c) return false;` as the first statement.

## finance/triple_ledger

**F-TL-NULL.** `triple_ledger_init(NULL)` and
`triple_ledger_export_conventional(tl, NULL)` dereference NULL. Patch: early
return on NULL in both (the other entry points also index `tl->accounts`
unguarded).

**F-TL-COVERAGE.** `triple_ledger_post` stores
`coverage_ratio = r*ell / 1.8`, and `triple_ledger_verify_coverage` compares
that ratio with 1.8 again, so an account passes only when `r*ell >= 3.24`,
while `e->coverage_verified` in the same file uses the intended
`r*ell >= 1.8`. Patch: compare `coverage_ratio` with 1
(`SR_CMP(a->coverage_ratio, SR_ONE) >= 0`).

## vino

**F-VINO-ADDR.** `vino_create_account` copies the address into
`address[64]` with truncation, but lookups compare the caller's full string,
so an address of 64 or more characters creates an account that no lookup can
find (and that still uses a slot). Patch: refuse it,
`if (fs_strlen(addr) >= sizeof v->balances[0].address) return -1;`.

**F-VINO-BALCAP.** `vino_get_balance` indexes `a->balance[cap]` with no bound
(`cap == CAP_MAX` reads past the array into `asset_balances`; UBSan's bounds
check reports it). Patch: `if ((uint32_t) cap >= CAP_MAX || !out) return -1;`.

**F-VINO-NULL.** `vino_get_account(v, NULL)` (and so `vino_transfer` with a
NULL address) dereferences NULL in `str_cmp`. Patch:
`if (!v || !addr) return 0;` at the top of `vino_get_account`.

## count_house / porter_house / community_chest

**F-CH-WRAP.** `count_house_deposit` stages
`bucket->token_balance = prior_balance + amount` with no overflow check: a
deposit past `UINT64_MAX` is accepted and wraps the balance to a small value.
Patch: `if (amount > UINT64_MAX - prior_balance) return -1;` before staging.

**F-PH-FULL.** An unsealed port admits everyone (by design: sealing is
opt-in), so when the seal table is full a refused
`porter_house_seal_port(..., PH_SEAL_TRUSTED, ...)` or
`porter_house_close_port` leaves that port open to every peer: the failure
mode is fail-open. Patch: when a seal or close is refused for capacity, record
the port in a small deny set (or set a `table_full` flag) and make
`porter_house_admit` refuse unsealed ports while it is set; or reuse OPEN
slots for seals as `close_port` already does.

**F-CC-REJECTED.** `cc_purchase_with_vouchers` and `cc_purchase_app` sell an
app whose signature was rejected (`state == CC_APP_REJECTED`): the buyer pays
and revenue is booked for an app that cannot be installed. Patch: refuse
unless `app->sig_verified` and the state is LISTED, VERIFIED or INSTALLED
(return -2, move nothing).

**F-CC-CASHIN-UNJOURNALED.** `cc_voucher_cash_in` / `cash_out` add to or
subtract from `acct->balance[capital]` directly and journal only a
zero-amount self-transfer, so vino's hash-chained journal cannot reproduce the
balances (the Tier 2 replay of `primary[]` fails at the first purchase, whose
buyer the journal never funded). Patch: journal the cash-in as a record that
carries the amount (an issue-style transaction, or a transfer from a
voucher-mint account) and the cash-out as the matching redemption.

## Freestanding allocators

**F-FS-ALIGN.** `fs_malloc` is a byte bump allocator: `fs_malloc(1)` followed
by `fs_malloc(8)` returns an address with `% 16 == 1`. Patch: round `n` (and
`heap_off`) up to `_Alignof(max_align_t)` before the bound check, with the
round-up itself checked for wrap.

**F-FS-PER-TU.** The heap is a `static` array inside a `static inline`
function in a header, so every translation unit that includes freestanding.h
gets its own private 1 MiB heap (the test fills one and a second unit still
allocates). Patch: move `fs_malloc`/`fs_calloc` and the heap into one .c file
(kernel/freestanding.c) with `extern` declarations in the header.

**F-MM-DFREE.** `kfree` of a block that is already free subtracts its size
from `heap_used` again and re-runs coalescing. Patch:
`if (block->free) return;` before marking it free.

**F-MM-FRAME0.** `page->frame == 0` means "no frame" to both
`mm_alloc_frame` and `mm_free_frame`, but frame 0 is the first one the bitmap
hands out: it can be allocated but never freed, and a page holding it is
allocated again. Patch: reserve frame 0 in `mm_init` (`bitmap_set(bm, 0)`,
counted as used) or test `page->present` instead of `page->frame`.

**F-MM-QUOTA.** `mm_free_frame` sets `page->frame = 0` before reading the
frame's RMAG quota, so the freed frame keeps its quota and frame 0 is charged
instead. Patch: save `uint32_t f = page->frame;` first and use `f` for the
bitmap and the quota.

**F-MM-ALIGN.** `kmalloc` rounds every size up to a multiple of 4, so after
`kmalloc(4)` the split-off header (which holds two pointers) sits at an
address `% 8 == 4`. mm.c is built into the 64-bit kernels (arm64, x86_64,
riscv), where that is a misaligned access (UB; a fault on strict-alignment
cores); UBSan reports it. Patch: round to `_Alignof(heap_block_t)`
(`size = (size + A - 1) & ~(A - 1)` with the `HEAP_MAX` check kept ahead of
it so the round-up cannot wrap).

**F-MM-SHIFT31.** The bitmap helpers shift the `int` 1 by up to 31
(`1 << 31` is signed overflow, UB; UBSan aborts once 32 frames are taken).
Patch: `1u << (bit % 32)` in `bitmap_set`, `bitmap_clear`, `bitmap_test` and
`bitmap_first_free`.
