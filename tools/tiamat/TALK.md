<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Talk to the Tiamat swarm (harness H1)

Three cells answer together: **Spinewyrm** (Qwen2.5-1.5B) leads general
questions, **Codelindworm** (Qwen2.5-Coder-1.5B) leads code questions, and
**Vouchwyvern** (SmolLM2-1.7B, a different model family) checks the answer.
Two-number sums ("17 times 23") are checked exactly instead.

Every answer ends with a line saying who answered and who checked it:

- `checked by Vouchwyvern (TRUE)` or `exact arithmetic: ... (TRUE)`: shown, checked.
- `Not checked: ...`: shown, but nothing could confirm it.
- `Held: ...`: the check found it wrong, or found evidence both ways. You see
  the reasons, not the answer. The answer itself is kept in the log.

The code is on branch `claude/project-thread-66qi8c` (commit 9c905b37) and
reaches `main` when Faster CI lands it. These steps work on `main` after that.

## 1. Get the three models (3.3 GB, once)

Same commands on the Mac and on the server. The pinned URLs are the exact
files checked in the manifest.

```sh
mkdir -p ~/zxv-models && cd ~/zxv-models
curl -LO https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf
curl -LO https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf
curl -LO https://huggingface.co/HuggingFaceTB/SmolLM2-1.7B-Instruct-GGUF/resolve/2d4a76a30b4af41ecd395c35725ac11688d4cfe4/smollm2-1.7b-instruct-q4_k_m.gguf
```

## 2. Write the swarm config (checks every file's SHA-256)

From the repository folder:

```sh
python3 tools/tiamat/make_h1_config.py --models-dir ~/zxv-models --out ~/zxv-models/tiamat.conf --verify
```

## 3. Build the app (about half a minute)

```sh
cd kernel
H=arch/hosted; T=$HOME/zxv-build; mkdir -p $T
python3 $H/gen_ui.py $H/zxv_ui.html $T/zxv_ui.c
cc -std=c11 -O2 -DZXV_HAVE_ZT_GLUE \
  $(for d in $(sh $H/app_sources.sh incs); do printf -- "-I%s " $d; done) \
  $(sh $H/app_sources.sh srcs) $H/zxv_zt_glue.c $T/zxv_ui.c \
  -o $T/zxv-host -lpthread -lm
```

## 4a. On the Mac

Ask one question in the terminal:

```sh
~/zxv-build/zxv-host --tiamat ~/zxv-models/tiamat.conf --ask "What is 17 times 23?"
```

Or chat in the window (it opens in your browser; type in the chat box):

```sh
~/zxv-build/zxv-host --tiamat ~/zxv-models/tiamat.conf
```

## 4b. On the server

On the server:

```sh
~/zxv-build/zxv-host --server --tiamat ~/zxv-models/tiamat.conf
```

It prints a line starting `ZXV-URL:` with a `#token=...` part (keep it
private). It listens on the server's own 127.0.0.1:8722 only. On the Mac:

```sh
ssh -L 8722:127.0.0.1:8722 you@server
```

then open that ZXV-URL in the Mac's browser and chat. Or ask from a shell on
the server, with the token from the ZXV-URL line:

```sh
curl -s -X POST -H "Host: localhost:8722" -H "X-ZXV-Token: TOKEN" \
  --data-binary "Who discovered penicillin?" http://127.0.0.1:8722/api/ask
```

## 5. The log

Every question is one JSON line in `~/zxv-models/tiamat_log.jsonl`: the
question, both cells' replies (held ones included), each verdict and the
time. The last one, readable:

```sh
tail -n 1 ~/zxv-models/tiamat_log.jsonl | python3 -m json.tool
```

## What to expect (measured on a 4-core cloud CPU)

16 questions: 14 right and shown, 1 wrong and held (1234 x 5678), 1 right but
held (a bare "100" the witness called false), none wrong and shown. Each
answer took 3 to 25 seconds. Replies stop at 256 new tokens, so long code is
cut. On the Mac the cells run on the CPU cores today: the hosted app does not
hand the engine's matrix products to the Metal GPU yet.
