<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tiamat behaviour tests on the A6000 server

The jobs below run `tools/tiamat/TEST_PLAN.md` T1 to T4 at full scale on the
server. Pass marks are the ones fixed in TEST_PLAN.md before the first run;
nothing here changes them. Every job keeps its raw output: copy the whole
`$OUT` folder (JSON, JSONL, logs; never weights) to
`/mnt/project-files/zxv-tests/tiamat-server/` and report failures as plainly
as passes.

```sh
export REPO=~/ZEDEC-XERO-VULPINE-V1.0.0 OUT=/srv/tiamat/tests/$(date -u +%F) M=/srv/tiamat/models
mkdir -p $OUT $M
```

## J0 Build the app (once)

```sh
cd $REPO/kernel && H=arch/hosted && mkdir -p /srv/tiamat/bin
python3 $H/gen_ui.py $H/zxv_ui.html /srv/tiamat/bin/zxv_ui.c
cc -std=c11 -O2 -DZXV_HAVE_ZT_GLUE $(for d in $(sh $H/app_sources.sh incs); do printf -- "-I%s " $d; done) \
  $(sh $H/app_sources.sh srcs) $H/zxv_zt_glue.c /srv/tiamat/bin/zxv_ui.c -o /srv/tiamat/bin/zxv-host -lpthread -lm
sh $H/test_hosted.sh 2>&1 | tee $OUT/j0_test_hosted.log      # must end [PASS]
```

## J1 The H1 swarm's models (3.3 GB) and its config

```sh
cd $M
curl -LO https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf
curl -LO https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf
curl -LO https://huggingface.co/HuggingFaceTB/SmolLM2-1.7B-Instruct-GGUF/resolve/2d4a76a30b4af41ecd395c35725ac11688d4cfe4/smollm2-1.7b-instruct-q4_k_m.gguf
python3 $REPO/tools/tiamat/make_h1_config.py --models-dir $M --out $OUT/h1.conf --log $OUT/h1_log.jsonl --verify
```

## J2 T2 scenarios at server speed

```sh
python3 $REPO/tools/tiamat/bench/run_t2.py --host /srv/tiamat/bin/zxv-host --conf $OUT/h1.conf --out $OUT/t2 \
  | tee $OUT/t2_summary.txt
```

Same sets and seed as the cloud run in `bench/results/2026-10-15-tiamat/t2_h1`,
so the two can be compared line by line (greedy decoding on an integer
engine: the replies should be identical).

## J3 T4 profile on the server, and the cross-machine check

```sh
python3 $REPO/tools/tiamat/bench/run_t4.py --host /srv/tiamat/bin/zxv-host --conf $OUT/h1.conf --out $OUT/t4 \
  --budget-bytes 48000000000 --compare $REPO/bench/results/2026-10-15-tiamat/t4_cloud/t4_results.json \
  | tee $OUT/t4_summary.txt
```

The `--compare` line is T4's "bit-identical on every machine" check: every
reply's SHA-256 must equal the cloud machine's.

## J4 T1 evidence head: probe (S5), then the native adapter (S6)

Needs `torch`, `transformers` and `peft`, and the two disc trunks' SafeTensors
(about 4 GB; no other cell is downloaded):
`python3 server/fetch_pinned.py --dest $M --placement disc --source --only Spinewyrm Kindlewyrm`.

```sh
cd $REPO/tools/tiamat
python3 server/make_tfbn_data.py --n 40000 --seed 1 --out /srv/tiamat/tfbn.jsonl
python3 bench/make_t1_set.py --out $OUT/t1_handset.jsonl
for cell in Spinewyrm Kindlewyrm; do
  python3 server/train_tfbn.py probe --model $M/$cell/src --data /srv/tiamat/tfbn.jsonl \
    --extra-test $OUT/t1_handset.jsonl --out /srv/tiamat/evh/$cell 2>&1 | tee $OUT/t1_probe_$cell.log
  cp /srv/tiamat/evh/$cell/report.json $OUT/t1_probe_$cell.json
done
python3 server/train_tfbn.py adapter --model $M/Spinewyrm/src --split-layer 14 \
  --data /srv/tiamat/tfbn.jsonl --extra-test $OUT/t1_handset.jsonl --out /srv/tiamat/tfbn/Spinewyrm \
  2>&1 | tee $OUT/t1_adapter_Spinewyrm.log
cp /srv/tiamat/tfbn/Spinewyrm/report.json $OUT/t1_adapter_Spinewyrm.json
```

Pass marks (T1): accuracy at least 0.85 and false-certainty rate at most 0.05
on the held-out split and on the hand-written set; the adapter beats the
probe on false certainty without losing more than 1 point of accuracy. The
engine-versus-float agreement mark (0.99) needs engine task E1 and is
reported as not run until E1 exists.

## J5 The full disc set and T3's adapter gate (approved 2026-10-15)

The owner approved the disc set and its downloads. Fetch every disc and
gated cell with its sources (S1), then turn the same-base fine-tunes into
adapters (S3). S3's report is T3's extraction gate: top-1 agreement at least
0.97 and KL at most 0.05 per cell.

Rerun after the first J5 (2026-10-15 12:11): S1 now also fetches a sharded
checkpoint's index and an upstream LoRA's `adapter_config.json`
(Theoremwyrm and Reviewdrake failed without them), and S3 reports each cell
and goes on: ADAPTER (extracted, passes the gate), UPSTREAM_ADAPTER (already
a LoRA on the tissue's base, copied as it is), FULL (ships whole) or ERROR
(with the reason). One report per tissue, named for its trunk:
`report_Spinewyrm.json`, `report_Numberwyrm.json`, `report_Kindlewyrm.json`,
`report_Awllindworm.json`.

```sh
cd $REPO/tools/tiamat/server
python3 test_extract_adapters.py 2>&1 | tee $OUT/s3_selftest.log       # must end [PASS]
python3 fetch_pinned.py --dest $M --placement disc disc_gated --source 2>&1 | tee $OUT/s1_fetch.log
for base in Qwen/Qwen2.5-1.5B-Instruct Qwen/Qwen2.5-Math-1.5B Qwen/Qwen2.5-0.5B-Instruct Qwen/Qwen2.5-Coder-0.5B-Instruct; do
  python3 extract_adapters.py --models $M --out /srv/tiamat/adapters --tissue $base 2>&1 | grep -v torch_dtype | tee -a $OUT/s3_adapters.log
done
cp /srv/tiamat/adapters/report_*.json $OUT/
```

Then the disc's adapter files and their hashes (S9): each adapter converted
to the engine's GGUF LoRA with llama.cpp (pinned at tag b6500, commit
a7a98e0f; a build tool only, nothing of it ships), written with its SHA-256
and size into `golden.models`:

```sh
git clone --depth 1 --branch b6500 https://github.com/ggml-org/llama.cpp /srv/tiamat/llama.cpp
git -C /srv/tiamat/llama.cpp rev-parse HEAD        # must print a7a98e0fffed794396b3fbad4dcdbbc184963645
pip install -e /srv/tiamat/llama.cpp/gguf-py
LLAMA_CPP=/srv/tiamat/llama.cpp $REPO/tools/tiamat/server/build_disc_models.sh /srv/tiamat 2>&1 | tee $OUT/s9_build.log
cp /srv/tiamat/golden.models $OUT/golden.models
grep -- '-lora-f16.gguf' /srv/tiamat/golden.models | tee $OUT/adapters_golden.txt
```

`adapters_golden.txt` is one line per disc adapter: file, SHA-256, size.
Those are the numbers the disc's golden list needs; a cell with no line
ships whole in a pack.

T3's task-set comparison (each tuned cell against its upstream model on that
cell's own tasks, plus a 100-question canary) needs per-cell task sets that
are not written yet; report it as not run.

## J6 Stress: a long session

```sh
yes "What is the capital of Australia?" | head -500 > $OUT/stress.txt
/srv/tiamat/bin/zxv-host --tiamat $OUT/h1.conf --ask-file $OUT/stress.txt --state-dir "" --notify off \
  > $OUT/stress.jsonl 2> $OUT/stress.stderr.txt
python3 -c "import json,sys; L=[json.loads(l) for l in open('$OUT/stress.jsonl')]; print(len(L), 'asked,', sum(x['lead_rc']==1 for x in L), 'answered')"
```

500 questions in one process: every one must be answered. Run nothing else
on the machine meanwhile: the first cloud T2 run stopped answering after
about ten questions, and a later run with a stray host running alongside got
0 of 10 (`bench/results/2026-10-15-tiamat/BUDGET_FINDINGS.md`). Also run J6
once more with J2 running at the same time and report how many were
answered: that measures how the swarm shares a busy machine.
