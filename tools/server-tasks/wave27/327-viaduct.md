# Wave 27 task: Viaduct (slot 327), Cross-currency settlement
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (073 coffer), wave 8 (077 trickle), wave 14 (171 mercer), wave 19 (234 neap), wave 20 (241 shallows).
Manifest: `slot 327`, `layer L3`, `icon icons/l3/327-viaduct.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: settles payments between currencies, co-ops and networks with fair rates and clear records (`ledger.accounts`, `ledger.post`, `ledger.hold`, `ledger.release`, `netting.open`, `netting.positions`, `netting.close`, `swap.quote`).
Uses: `use shallows 1` (translated captions, for counterparties' languages); `use neap 1` ("entry may post", A = L + E); `use mercer 1` (sale records); `use trickle 1` (amount due at a tick, "stream allowed"); `use coffer 1` ("transfer allowed"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `transfer_ok` (amount at most the per-day limit AND the counterparty known AND the rate inside its declared band) in Ada as Eve + COBOL as Sutra witnessed; `exchange` (amount x rate num / rate den with the remainder carried) in FORTRAN as Trebago + COBOL as Sutra witnessed; `settlement_ledger` (in minor units, refused on overflow) in COBOL as Sutra; `currencies` grid in APL as KRON; `counterparties` tree in MUMPS as MYRA.
BOTH test: payment 77 shows sent on our side and not received on theirs: it is BOTH and held in escrow (`ledger.hold`) until reconciled. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Financial: every transfer is the owner's act; regulated switches stay as the owner set them.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, peers, net, notify.
Planned (no runtime yet): Erlang.
Pending: Erlang (messaging): no entry here is an obvious fit; put `pending Erlang <entry>` on the one closest to messaging and name it in your report.
