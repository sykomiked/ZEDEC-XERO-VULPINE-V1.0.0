# Wave 6 task: Trickle (slot 77), Streaming payments and tithes
Read ../APP_WAVE_SPEC.md first.
What it does: standing streams and tithes the owner sets up, paid by `ledger.post` per period; read back with `ledger.entries`; `estate.stream`'s cumulative rule (floor(total x share), held-back units carried) is the pattern. No stream method exists: use only existing methods; list what's missing in your report.
Logic (critical): amount due at tick t = floor(total x t / T) minus already paid, in sutra + trebago witnessed; a tithe = floor(income x num / den) with the carried remainder in sutra + trebago witnessed; "stream allowed" (under the owner's per-day cap and balance) in eve + sutra witnessed.
A stream starts, changes or stops only on the owner's approval; no unit is lost to rounding (remainders carry).
needs: ledger.read, ledger.post, notify.
Planned (no runtime yet): Scala, Eiffel, Solidity.
