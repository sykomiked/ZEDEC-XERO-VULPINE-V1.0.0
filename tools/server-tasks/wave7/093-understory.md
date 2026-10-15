# Wave 7 task: Understory (slot 93), Nature budgets
Read ../APP_WAVE_SPEC.md first.
What it does: energy, water and land budgets: energy from `power.status`, `power.top`, set with `power.budget` (feature `power`, off by default). No water or land method: use only existing methods; list what's missing in your report.
Logic (critical): energy cost (energy x tariff, minor units) in sutra + C++ wasm witnessed; "over budget" truth in eve + kron (J) witnessed; rolling-window totals exact in trebago; water and land entries in myra.
Advisory; setting a budget needs the owner's confirmation; never controls the grid.
needs: sys.read, notify.
