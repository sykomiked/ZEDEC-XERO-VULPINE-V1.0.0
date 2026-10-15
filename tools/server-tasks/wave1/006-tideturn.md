# Wave 1 task: Tideturn (slot 6), Software updates
Read ../APP_WAVE_SPEC.md first.
What it does: dual-signed OS and app updates with an audit trail (`update.check`, `update`).
Logic: version ordering (major.minor.patch compare) in sutra + kron witnessed; "both signatures present and valid" as a truth in eve + kron (inputs are the two verify results as 0/1); update record in myra.
Never install anything in tests; use the daemon's check only.
needs: net, sys.read.
