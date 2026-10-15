# R24 end to end: idle self-work and sealed receipts (2026-10-15, cloud, 4 cores)

Binary: `zxv-host` built from this branch's R24 commit, cells from
`tools/tiamat/make_h1_config.py` (slot lines carry the manifest's SHA-256).

## Batch (`--ask-file`, then `--ask` appending to the same file)

`batch_receipts.jsonl`: four receipts, seq 1-4. Three came from a three-line
`--ask-file` run (`batch_answers.jsonl`), the fourth from a separate `--ask`
run that resumed the chain (seq 4, prev = receipt 3's hash). Each `hash` is
SHA-256 of the line up to `,"hash"` with `}` appended (E2); each `prev` is
the previous hash. Checked independently in Python: chain ok.

## Server (`--server`, two HTTP asks, one planted bad pin)

The config pinned slot 0 with its true hash's last digit changed (…407f for
…407e), so its pin check must fail, and only after reading the whole file.

- 40 s after start a person asked "What is 12 times 12?". Idle work was
  hashing slot 0 at 368,050,176 bytes; the answer came back within 1 s
  (receipt 1: `"paused":"pin 0 at 368050176 bytes"`).
- The pin went on from where it stopped and reported the planted mismatch
  after the full 1,117,320,736 bytes (`server_stderr.txt`): idle work yields
  to the person, then resumes rather than restarting or skipping.
- 150 s later a second ask paused the slot 1 pin at 642,777,088 bytes
  (receipt 2). No failure was reported for slot 1 before or after.
- Idle slices are paid by stand-in agents; the companion's tokens are only
  the answers' (`tokens` in each receipt).

zxv-host receipts are hash-chained, unsigned; under zxvd each receipt is
signed with the chat ML-DSA-65 key (`sig`, `signer`).
