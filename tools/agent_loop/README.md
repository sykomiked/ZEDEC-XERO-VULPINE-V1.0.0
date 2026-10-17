# agent_loop — the A6000 coding engine

A checkpointed, single-model task runner: one Qwen model on one machine
works through a queue of coding-adjacent tasks, writing each answer to
`results/` with its prompt hash, seed and model id, so every output is
reproducible. The loop is meant to run unattended on the A6000 while a
human (or a Devin session) reviews `results/` and feeds the queue.

Nothing generated here edits the repository on its own. The model's
outputs are *proposals*: they land in `results/` for review. That bound
is deliberate — a 1.5B model is a drafting engine, not an autonomous
committer, and the repo's own rule is that a change is done when its
checks pass, not when it is written.

## Layout

    queue.yaml      the work list; each entry: id, title, task, files[],
                    context (optional), tokens (max new tokens)
    run.sh          the runner: for each pending task, prompt the model,
                    save the reply, mark the task done (atomic rename)
    results/        NN_title.txt per finished task + a run.log
    README.md       this file

## Run

    # one pass over the queue (stops when everything is done)
    ./run.sh --once

    # keep going until killed (re-checks the queue every cycle)
    ./run.sh

State lives in `queue.state` next to `queue.yaml`: a plain list of
finished task ids. `--once` and a fresh state file mean the loop is
safe to restart at any point — finished tasks are never re-run.

## Configuration (environment)

    LLAMA_BIN   path to llama-cli            (default: $HOME/llama.cpp/llama-cli)
    LLAMA_MODEL path to the .gguf            (default: $HOME/zxv-cuda/models/qwen2.5-1.5b-instruct-q4_k_m.gguf)
    LLAMA_NGPU  GPU layers, 0 = CPU          (default: 0)
    LLAMA_CTX   context size                 (default: 8192)
    LLAMA_SEED  fixed seed for determinism   (default: 0x5A5856 = ZXV)

## Determinism

Each task records: model path, seed, prompt SHA-256, llama.cpp commit.
Same queue + same model + same seed -> same bytes, which is what the
repo's deterministic doctrine demands of any generated artifact.