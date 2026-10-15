# Wave 1 task: Witness (slot 18), Audit log
Read ../APP_WAVE_SPEC.md first.
What it does: signed record of what changed, who approved it, held crossings, accepted AI actions (`chronicle`, `provenance`, `peer_audit`, log.* / journal).
Logic: entry record in myra; counts by kind in kron + trebago witnessed; "chain intact" (each entry's prev index equals the previous entry's index) in kron + sutra witnessed over integer vectors.
needs: journal, sys.read.
