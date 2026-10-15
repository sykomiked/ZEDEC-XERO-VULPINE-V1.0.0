# Wave 5 task: Treadle (slot 24), Automation
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 24, build position 54 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: run a task on a schedule or an event, with the owner's limits (`event_sched`, swarm budgets).
Logic (critical): next run time (minutes of day, period) in sutra + kron + C++ wasm witnessed; "within budget" truth in eve + kron witnessed; task record in myra. Nothing runs without the owner's approval (AI suggestions held, M3).
needs: notify, swarm.
