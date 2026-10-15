# Wave 7 task: Bellows (slot 59), AI image and voice generation
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 59, build position 79 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: requests through the companion (`swarm.ask`, `model.info`) under the owner's budgets (`power.budget`, `power.status`), results held until accepted (M3), then saved (`files.write`). Image and voice generation have no method: use only existing methods; list what's missing in your report.
Logic (critical): "within budget" in eve + kron witnessed; token cost in sutra + kron witnessed (denominations from kernel/src/swarm/swarm_denom.h, refused on overflow); energy of a request (microjoules from `power.status`) exact in trebago; request record in myra.
Never imitate a real person's voice or face without their recorded consent.
needs: ai.ask, swarm, fs.write, notify.
Planned (no runtime yet): Python, MATLAB, LabVIEW.
