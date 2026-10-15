# Wave 6 task: Gavel (slot 74), V-Bill auctions
Read ../APP_WAVE_SPEC.md first.
What it does: epoch state, quotes, bids and cancels (`auction.status`, `auction.quote`, `auction.bid`, `auction.cancel`, `auction.check`). Feature `auctions` is regulated and off by default (-32012); the app says so and never turns it on.
Logic (critical): the amount a bid locks (qty x limit, minor units) in sutra + trebago witnessed; "bid allowed" (limit at or above the floor, lock within the owner's cap and balance) in eve + sutra witnessed; demand at each quoted price (sum over bids) exact in trebago. Clearing stays the daemon's; the app shows `auction.run`'s result, never recomputes it.
A bid is a financial action: the owner approves each one. Tests call `auction.quote`, `auction.status`, `auction.check` only, with the feature off.
needs: ledger.read, ledger.post, notify.
Planned (no runtime yet): Eiffel, Solidity, PHP.
