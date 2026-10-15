# Server runs J0-J6 (A6000 VM, 2026-10-15)

Raw output of `tools/tiamat/server/TESTS.md` J0-J6 as the server wrote it
(no weights). `jobs.log` has each job's start, end and exit code.

- J4 (T1 evidence head): the held-out split passes (Spinewyrm probe 0.993
  accuracy, 0.005 false certainty; adapter 0.998 / 0.0015); the hand-written
  set fails the marks (Spinewyrm probe 0.59 / 0.45, Kindlewyrm probe
  0.56 / 0.34, Spinewyrm adapter 0.765 / 0.28): the synthetic training data
  does not transfer.
- J5 (S1, S3): S1 verified every disc cell. S3 stopped three of its four
  tissues: Reviewdrake (an upstream LoRA, fetched without its
  adapter_config.json), Theoremwyrm (shards fetched without their index) and
  the 0.5B tissue (its report folder did not exist when no cell passed).
  `report_Awllindworm.json` is the Coder-0.5B tissue's report (named for its
  trunk): Recordlindworm passes as an adapter (top-1 0.971, KL 0.0086),
  Mendlindworm ships whole. Wardendrake and Statutewyrmling reached at most
  0.91 and 0.79 top-1 before the stops. The fixes and the rerun are J5 in
  TESTS.md.
