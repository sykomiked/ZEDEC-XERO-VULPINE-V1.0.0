# Wave 1 task: Treadle (slot 24), Automation
Read ../APP_WAVE_SPEC.md first.
What it does: run a task on a schedule or an event, with the owner's limits (`event_sched`, swarm budgets).
Logic (critical): next run time (minutes of day, period) in sutra + kron + C++ wasm witnessed; "within budget" truth in eve + kron witnessed; task record in myra. Nothing runs without the owner's approval (AI suggestions held, M3).
needs: notify, swarm.
