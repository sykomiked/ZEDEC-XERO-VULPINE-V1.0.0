# Wave 6 task: Ninefold (slot 80), The nine forms of capital
Read ../APP_WAVE_SPEC.md first.
What it does: what the owner holds in each of the nine forms (finance/capital_forms.h), from `ledger.accounts` and `ledger.balance`. No method reports holdings by form: use only existing methods; list what's missing in your report.
Logic (critical): holding per form in minor units in sutra + trebago witnessed; share x100 per form in trebago + sutra witnessed; "state-reserved form, never priced" truth (capital_is_state_reserved) in eve + trebago witnessed.
needs: ledger.read, swarm.
Planned (no runtime yet): PostScript, Eiffel, Solidity.
