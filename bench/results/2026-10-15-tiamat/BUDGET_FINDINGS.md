<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# The first T2 run's budget starvation: what was and was not shown

**Seen once.** The first T2 run (`t2_h1_run1_budget_starved/`) stopped answering
after about nine questions: every cell's per-cycle allotment read 0 and each ask
was refused as "budget too small". The load average at the time was 4.37 on 4
cores, and other processes of mine were running (a busy loop and, probably,
leftover zxv-host processes from an earlier killed run).

**Change made (commit 24a18d33).** The governor used the 1-minute load average as
"other load", which counts the app's own inference as load from someone else.
It now measures busy CPU over the interval since the last scan and subtracts the
app's own CPU time (Linux /proc/stat, macOS host_statistics, Windows
GetSystemTimes). That change is sound on its own terms and stays.

**Not confirmed as the cause.** That commit's message says the load average was
the cause. Three controlled before/after runs did not reproduce the starvation
with the old build:

| run | condition | old build | new build |
|---|---|---|---|
| `budget_ab_1core/` | one busy core, 4 cores | 20/20 | 20/20 |
| `budget_ab_2core/` | two busy cores, load about 5.3 | 30/30 | 30/30 |
| `budget_ab_restart/` | started right after a 30-question run | 5/5 | 5/5 |

A two-process test (two hosts at once) did not starve either. So the most likely
trigger was several stray processes pushing load well above the core count at a
scan, which the old build would read as "no free cores". That is a likely
explanation, not a proven one. The 500-question stress job (server J6) is the
check that matters: every question must be answered.

## Second sighting, by accident (with the whole-reply fix)

A T4 run (`t4_cloud_run2_concurrent/`) started while a stray T2 process from
a killed run was still running inference in another zxv-host. Every ask was
refused ("its budget for this cycle is too small for the message"), 0 of 10,
twice. With two hosts generating at once, the governor sees the other one as
load and grants each cycle less than a whole reply (prompt plus 256 tokens),
so a cell waits three cycles and gives up. That fits the stray-process
explanation above. It also shows the trade-off of the whole-reply fix: under
heavy load from other programs a cell now says it cannot answer instead of
showing a reply cut short.
