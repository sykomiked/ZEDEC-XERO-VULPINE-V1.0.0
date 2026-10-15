# Wave 1 task: Ferry (slot 12), Move to a new machine
Read ../APP_WAVE_SPEC.md first.
What it does: move files, spaces, keys (owner approves), settings (`files.cross`, `spaces.*`, Keepsake).
Logic: transfer plan totals (bytes, files) exact in trebago; progress x100 from the used_share block (`use used_share 1`); "fits on the target" truth in sutra + kron witnessed; per-space share in kron.
needs: fs.read, fs.write, peers, backup.
