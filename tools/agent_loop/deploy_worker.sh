#!/bin/sh
# deploy_worker.sh — set up a drafting worker on a fresh GPU box (the
# H100): the model server (vLLM, unquantized), the repo, the zxvd judge,
# and the pipeline. One script, run as root or with sudo.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh deploy_worker.sh <repo-tarball-url-or-path> [model]
#
# What it does, in order:
#   1. installs vLLM (pip) and starts the model server on :8000
#   2. unpacks the repo
#   3. builds the zxvd judge (the kernel's own app runtime) and starts it
#   4. launches the pipeline against the local model + judge
#   5. prints the progress file to tail
set -eu

REPO="${1:?usage: deploy_worker.sh <repo-tarball> [model]}"
MODEL="${2:-Qwen/Qwen2.5-Coder-32B-Instruct}"
WORK=/opt/zxv
mkdir -p "$WORK"
cd "$WORK"

echo "== 1/5 the model server (vLLM, unquantized) =="
if ! command -v vllm >/dev/null 2>&1; then
    pip install --quiet vllm 2>/dev/null || pip3 install --quiet vllm
fi
# unquantized BF16: the 32B coder fits one 80 GB card whole
setsid nohup vllm serve "$MODEL" \
    --host 127.0.0.1 --port 8000 \
    --max-model-len 8192 \
    --gpu-memory-utilization 0.90 \
    > /tmp/vllm.log 2>&1 < /dev/null &
echo "   vLLM starting (tail /tmp/vllm.log); waiting for /v1/models..."
for i in $(seq 1 120); do
    if curl -sf -m 3 http://127.0.0.1:8000/v1/models >/dev/null 2>&1; then break; fi
    sleep 5
done
curl -s -m 5 http://127.0.0.1:8000/v1/models | head -c 120 || echo "   (vLLM not up yet — check /tmp/vllm.log)"

echo "== 2/5 the repo =="
if [ -f "$REPO" ]; then
    tar xzf "$REPO" -C "$WORK"
else
    curl -fsSL "$REPO" -o /tmp/zxv-repo.tgz && tar xzf /tmp/zxv-repo.tgz -C "$WORK"
fi
SRC="$(find "$WORK" -maxdepth 2 -name src-int -type d | head -1)"
[ -n "$SRC" ] || { echo "no src-int in the tarball"; exit 1; }

echo "== 3/5 the zxvd judge =="
cd "$SRC"
sh kernel/arch/hosted/build_zxvd.sh /opt/zxv/zxvd-out >/tmp/zxvd-build.log 2>&1 || {
    echo "zxvd build failed; see /tmp/zxvd-build.log"; exit 1; }
mkdir -p /opt/zxv/rpc && chmod 700 /opt/zxv/rpc
setsid nohup /opt/zxv/zxvd-out/zxvd --no-lan-discovery --port 8799 \
    --rpc /opt/zxv/rpc/z.sock > /tmp/zxvd.log 2>&1 < /dev/null &
sleep 5
TOK=$(grep -oE "token=[0-9a-f]{64}" /tmp/zxvd.log | tail -1 | cut -d= -f2)
echo "   judge token: ${#TOK} chars"

echo "== 4/5 the pipeline =="
cd "$SRC/tools/agent_loop"
setsid nohup env \
    VLLM_URL=http://127.0.0.1:8000 \
    VLLM_MODEL="$MODEL" \
    ZXVD_URL=http://127.0.0.1:8799 \
    ZXVD_TOKEN="$TOK" \
    python3 pipeline.py --attempts 6 \
    > /opt/zxv/pipeline.out 2>&1 < /dev/null &
sleep 20

echo "== 5/5 running =="
echo "   progress: tail -f /opt/zxv/pipeline.out"
head -3 /opt/zxv/pipeline.out 2>/dev/null || true
echo "done."
