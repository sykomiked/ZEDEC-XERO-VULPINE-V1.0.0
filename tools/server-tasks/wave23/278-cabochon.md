# Wave 23 task: Cabochon (slot 278), Household treasury
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 4 (073 coffer), wave 8 (082 larder), wave 12 (145 cartulary), wave 14 (171 mercer), wave 19 (234 neap).
Manifest: `slot 278`, `layer L3`, `icon icons/l3/278-cabochon.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: gathers accounts, savings goals, bills and investments into one honest picture of your wealth (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.holds`).
Uses: `use neap 1` ("entry may post", A = L + E); `use mercer 1` (stock and sale records for a household shop); `use cartulary 1` (retention days for statements); `use coffer 1` (balance per account, "transfer allowed"); `use larder 1` (spent per category, "over budget"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `net_worth` (assets minus liabilities in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; `goal_months` (months to a savings goal at the declared monthly saving and rate, floor) in FORTRAN as Trebago + COBOL as Sutra witnessed; `cap_ok` (spending in a category at most its declared cap) in Ada as Eve + APL as KRON witnessed; `portfolio` grid in APL as KRON; `family_accounts` tree in MUMPS as MYRA.
BOTH test: the bank statement says 1520.00 and your own books 1480.00 for one account: the balance is BOTH, flagged for review, and neither record is overwritten. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Financial: advisory, not financial advice; the app never moves money.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, fs.read, fs.write, notify.
Planned (no runtime yet): Objective-C.
Pending: `pending Objective-C family_accounts` (objects).
