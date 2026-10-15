# Wave 1 task: Talisman (slot 76), Player Cards
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 76, build position 6 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Coffer (layer 73, wave 4, a later wave (a cycle): `use coffer 1`); Kitling (layer 62, wave 1, an earlier wave: `use kitling 1`); Ninefold (layer 80, wave 1, a later wave (a cycle): `use ninefold 1`); Vintner (layer 91, wave 1, a later wave (a cycle): `use vintner 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 5 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: the owner's Player Card: tier, each input with its source, next tier's thresholds, perks (`player.card`).
Logic (critical): progress x100 of each input toward the next tier in sutra + trebago witnessed; "tier earned" truth (every input at or above its threshold) in eve + trebago witnessed. Inputs the daemon reports as 0 with a reason are shown as missing, never as earned. History of tiers kept in the C model.
Tiers give no priority, discount or credit (as `player.card` says); the app never implies otherwise.
needs: ledger.read, sys.read.
Planned (no runtime yet): Eiffel, Solidity, RPG.
