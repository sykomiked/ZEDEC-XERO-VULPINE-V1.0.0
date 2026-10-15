# Wave 2 task: Lifeboat (slot 11), Recovery
Read ../APP_WAVE_SPEC.md first.
What it does: start safe (every switch off: `features.get`, `features.set`), check (`journal.verify`, `ledger.check`, `sys.disk`, `backup.verify`), restore from Keepsake (`backup.list`, `backup.restore`), check for a signed reinstall (`update.check` only). Bare-metal reinstall and boot-time repair of the Hoard have no method: use only existing methods; list what's missing in your report.
Logic (critical): "safe to start normally" from the check results (journal intact, books balanced, free bytes above floor, backup tested, each 0/1 or a count) in forth + eve witnessed; next repair step from those results in basic + kron witnessed; "restore fits" (bytes needed vs free) in sutra + kron witnessed; recovery record in myra.
Nothing is restored, switched or reinstalled without the owner's approval; restore always makes a new item. Tests use a scratch Hoard and books, never the real ones.
needs: sys.read, fs.read, fs.write, backup, journal, ledger.read, notify.
Planned (no runtime yet): Elixir, Crystal.
