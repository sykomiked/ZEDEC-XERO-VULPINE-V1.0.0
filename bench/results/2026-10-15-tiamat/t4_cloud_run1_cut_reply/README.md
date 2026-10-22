<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# T4 first cloud run: the same question got two different replies

Marks: first answer warm 3.3 s (pass, mark 20 s); resident peak 3.14 GB
(pass, mark 4 GB); 10 of 10 answered twice (pass); same answers twice: FAIL.

Question 7, "What is 9876 minus 1234?":
- batch 1: "...you subtract the smaller number from the larger number:\n\n9876 - 1234 = 8642"
  (shown, exact arithmetic TRUE)
- batch 2: "...you subtract the smaller number from the larger number"
  (cut off; held, exact arithmetic FALSE). See batch_2.jsonl.

Cause, measured with a copy of the host that logs the budget gate: the gate
(zxv_budget_gate.h B2) lets a model write at most what is left of the cycle's
allotment, so late in a cycle a reply is cut short (max_new fell to 45, then 0,
in a 10-question run). The cut reply was then judged as if complete. The
second run of T2 (t2_h1_run2_cut_replies) has the same cut replies, for
example "To find the product of 928 and 88, you".

Fix: a Tiamat cell now runs only when the cycle can pay for a whole reply
(prompt plus 256 tokens) and otherwise waits for the next cycle. With the fix,
the logging copy showed every one of 18 calls granted the full 256 tokens, and
two runs gave identical replies.

batch_1.jsonl and the timing files were overwritten by mistake when the rerun
started in the same folder; t4_results.json and batch_2.jsonl are the
original run's.
