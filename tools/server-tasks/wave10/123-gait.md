# Wave 10 task: Gait (slot 123), Profiler
Read ../APP_WAVE_SPEC.md first.
What it does: where time and energy go: consumers by energy (`power.top`, `power.status`), the swarm's cycles (`swarm.state`), machine facts (`sys.info`), events (`log.tail`). zxvd has no sampling-profiler method: use only existing methods; list what's missing in your report.
Logic (critical): share x100 of each consumer in kron + trebago witnessed; p50 and p95 over a sorted vector of ms in kron + trebago witnessed; energy per consumer summed in integer mJ, refused on overflow, in sutra + trebago witnessed; sampling limits (1..1000 Hz, at most 65536 samples) in eve; hot-spot record (name, samples, energy) in myra.
needs: sys.read.
Planned (no runtime yet): Racket, ML, Bash.
