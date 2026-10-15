# Wave 14 task: Mercer (slot 171), Small shop
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (076 talisman, 109 tally), wave 2 (079 fairground), wave 4 (073 coffer, 078 tallystick).
Manifest: `slot 171`, `layer L2`, `icon icons/l2/171-mercer.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: runs a small shop: stock, prices, sales and receipts in the books (`ledger.post`, `ledger.balance`, `ledger.entries`) and the Hoard (`files.read`, `files.write`), receipts printed by `print.submit`, market listings from `net.offers`, the seller's card from `player.card`.
Uses: `use coffer 1` (balance, "transfer allowed"); `use tallystick 1` (invoice total, "paid in full", receipt numbering); `use fairground 1` (the assurance fee, "offer allowed", price per unit); `use tally 1` (currency amount, rationals); `use talisman 1` ("tier earned", shown on listings). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): stock after a sale (never below zero, refused) in COBOL as Sutra + APL as KRON witnessed; price from cost and margin (cost x (den + num) / den with the stated rounding) in FORTRAN as Trebago + COBOL as Sutra witnessed; "sale on credit allowed" (the customer's balance plus the sale at most their declared limit) in Ada as Eve + COBOL as Sutra witnessed; stock grid in APL as KRON; product tree in MUMPS as MYRA.
BOTH test: the till records sale 88 as 3 units for 1500 and the buyer's receipt says 2 units for 1000: the sale is BOTH, held, and not posted until reconciled, so the books stay balanced.
Financial: every posting is the owner's act; the app never moves money by itself.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, fs.read, fs.write, print, net, peers.
Planned (no runtime yet): LISP.
