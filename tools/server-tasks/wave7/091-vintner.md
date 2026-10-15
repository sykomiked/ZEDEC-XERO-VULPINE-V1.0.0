# Wave 7 task: Vintner (slot 91), Vineyard and wine ledger
Read ../APP_WAVE_SPEC.md first.
What it does: vineyard lots, barrels, bottling and sales in the books (`ledger.open_account`, `ledger.post`, `ledger.entries`, `ledger.balance`); barrel lot labels from `grants.status`.
Logic (critical): sale value (bottles x price, minor units) in sutra + C++ wasm witnessed; volume through the stages (harvest, must, barrel, bottle, losses) conserved exactly in trebago; cellar probe readings (raw counts to tenths of a degree, range check) in forth; lots and vintages in myra.
Posting a sale needs the owner's confirmation.
needs: ledger.read, ledger.post.
