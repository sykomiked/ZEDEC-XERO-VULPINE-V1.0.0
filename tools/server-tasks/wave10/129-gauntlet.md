# Wave 10 task: Gauntlet (slot 129), Mutation testing
Read ../APP_WAVE_SPEC.md first.
What it does: mutate code, rerun its tests, show the survivors (tests/harness/mutator); mutants of Playground templates run with `play.run`. zxvd has no mutation-run method: use only existing methods; list what's missing in your report.
Logic (critical): mutation score x100 (killed / (total - equivalent)) in kron + trebago witnessed; counts add up (killed + survived + equivalent = total, refused when not) in sutra + kron witnessed; gate (score at least the target's floor, at most 10000 mutants) in eve; survivor record (file, line, operator) in myra.
Mutants never touch committed files; they run on copies.
needs: fs.read.
Planned (no runtime yet): Raku, ML, JCL.
