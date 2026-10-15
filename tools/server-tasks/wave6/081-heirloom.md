# Wave 6 task: Heirloom (slot 81), Estate plans and recovery guardians
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 81, build position 65 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Coffer (layer 73, wave 4, an earlier wave: `use coffer 1`); the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: the inheritance manifest, guardians and recovery (`estate.setup`, `estate.manifest`, `estate.faraid`, `estate.distribute`, `estate.status`, `recovery.setup`, `recovery.status`, `recovery.claim`).
Logic (critical): manifest fractions sum to exactly 1 (cross-multiplied) in sutra + trebago witnessed; split preview by largest remainder in sutra + trebago witnessed, and compared with `estate.distribute` (reports only); "release due" (k guardian approvals and the waiting period over, no living veto) in eve + trebago witnessed.
Nothing happens on time or silence alone; owner and guardians sign. Tests make their own throwaway keys, never anything from build_system/keys/.
needs: ledger.read, peers, notify.
Planned (no runtime yet): Elixir, Simula I, Oberon.
