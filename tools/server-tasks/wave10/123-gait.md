# Wave 10 task: Gait (slot 123), Profiler
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 123, build position 116 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: where time and energy go: consumers by energy (`power.top`, `power.status`), the swarm's cycles (`swarm.state`), machine facts (`sys.info`), events (`log.tail`). zxvd has no sampling-profiler method: use only existing methods; list what's missing in your report.
Logic (critical): share x100 of each consumer in kron + trebago witnessed; p50 and p95 over a sorted vector of ms in kron + trebago witnessed; energy per consumer summed in integer mJ, refused on overflow, in sutra + trebago witnessed; sampling limits (1..1000 Hz, at most 65536 samples) in eve; hot-spot record (name, samples, energy) in myra.
needs: sys.read.
Planned (no runtime yet): Racket, ML, Bash.
