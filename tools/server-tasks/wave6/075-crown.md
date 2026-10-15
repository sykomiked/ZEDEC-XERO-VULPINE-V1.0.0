# Wave 6 task: Crown (slot 75), The Crown Allotment
Read ../APP_WAVE_SPEC.md first.
What it does: mutual endowment and distribution by sortition over the grant engine (`grants.status`, `grants.eval`, `grants.epoch`, `grants.pool_credit`, `grants.distribute`, `grants.account`). There is no draw method: use only existing methods; list what's missing in your report (a seeded, verifiable sortition draw).
Logic (critical): "in good standing" from `grants.eval`'s reason bits (no unappealed bit set) in eve + sutra witnessed; each recipient's grant split by weight, largest remainder, summing exactly to the pool, in sutra + trebago witnessed; draw order from a published seed and member count (integer modular steps, no hashing in the block) in trebago + sutra witnessed; pool and epoch totals exact in trebago.
Wording: "allotment" and "grant" in every user-visible string, never raffle, lottery or payout. Distribution is human-approved; tests use `grants.setup` and `grants.fund` sandbox units only (not money).
needs: ledger.read, ledger.post, notify.
Planned (no runtime yet): Go, Eiffel, Solidity.
