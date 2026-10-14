#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""check_workflow_tiers.py: keep draft pull requests on the fast tier.

Every workflow that runs on pull requests, other than ci.yml (which has its
own tiers) and codeql.yml, must skip its jobs on a draft and start again on
ready_for_review (docs/CI.md, rule 3). Without that, one kernel draft started
about 150 jobs and fifteen drafts starved the 20-job queue
(docs/CI_QUALITY_AUDIT_2026-10-12.md, RC4). This check fails when a workflow
adds a job, or a new workflow arrives, without the rule.

A job is covered when its `if:` mentions pull_request.draft, or when it
`needs:` another job (it is skipped when that job is skipped).
"""
import pathlib
import sys

import yaml

OWN_TIERS = {"ci.yml", "codeql.yml"}
# Small jobs allowed on drafts on purpose: workflow file -> job ids.
DRAFT_OK = {"zxvd.yml": {"e2e"}}


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent / ".github" / "workflows"
    bad = []
    for path in sorted(root.glob("*.yml")):
        if path.name in OWN_TIERS:
            continue
        wf = yaml.safe_load(path.read_text())
        on = wf.get(True, wf.get("on")) or {}
        if not isinstance(on, dict) or "pull_request" not in on:
            continue
        pr = on["pull_request"] or {}
        if "ready_for_review" not in (pr.get("types") or []):
            bad.append(f"{path.name}: pull_request needs types [opened, synchronize, reopened, ready_for_review]")
        for job, spec in (wf.get("jobs") or {}).items():
            if job in DRAFT_OK.get(path.name, set()) or "needs" in spec:
                continue
            if "pull_request.draft" not in str(spec.get("if", "")):
                bad.append(f"{path.name}: job {job} runs on drafts (add the draft `if:` from docs/CI.md rule 3)")
    for b in bad:
        print(b)
    if bad:
        print(f"FAIL: {len(bad)} workflow tier problem(s)")
        return 1
    print("workflow tiers OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
