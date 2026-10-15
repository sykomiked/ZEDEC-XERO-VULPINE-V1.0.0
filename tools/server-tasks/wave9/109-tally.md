# Wave 9 task: Tally (slot 109), Calculator
Read ../APP_WAVE_SPEC.md first.
What it does: integer and exact rational arithmetic (kernel/src/rational: reduced num/den, overflow refused), units, currency in minor units. Currencies the owner enabled come from `ledger.accounts`. zxvd has no exchange-rate method: use only existing methods; list what's missing in your report. Conversions are shown, never posted.
Logic (critical): rational add, subtract, multiply, divide, reduced, overflow refused, in trebago + sutra witnessed; currency amount (minor units x rate num / rate den, remainder shown) in sutra + kron witnessed; unit factors (num/den vectors by unit index, same dimension only) in kron.
Never a float: 1/3 stays 1/3.
needs: ledger.read.
Planned (no runtime yet): ALGOL 60, TRAC.
