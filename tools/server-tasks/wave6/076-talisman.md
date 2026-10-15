# Wave 6 task: Talisman (slot 76), Player Cards
Read ../APP_WAVE_SPEC.md first.
What it does: the owner's Player Card: tier, each input with its source, next tier's thresholds, perks (`player.card`).
Logic (critical): progress x100 of each input toward the next tier in sutra + trebago witnessed; "tier earned" truth (every input at or above its threshold) in eve + trebago witnessed. Inputs the daemon reports as 0 with a reason are shown as missing, never as earned. History of tiers kept in the C model.
Tiers give no priority, discount or credit (as `player.card` says); the app never implies otherwise.
needs: ledger.read, sys.read.
Planned (no runtime yet): Eiffel, Solidity, RPG.
