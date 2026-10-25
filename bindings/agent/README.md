# Agent manifest boundary

`manifest.py` validates the supplied `@SYSTEM-EXEC : CORE_AUDIT` manifest
subset and its `EVAL_CORRIDOR_SOLVENCY` dispatch identifier. Unsupported
actions, clauses and expressions fail instead of being silently skipped.
The manifest is data; parsing it does not dispatch a payment or run a model.

```python
from bindings.agent.manifest import parse_manifest, evaluate_coverage, allocate_quota

policy = parse_manifest(source)
result = evaluate_coverage(policy, "1.9", "0.97")
assert result.allowed  # 1843/1000 >= 9/5, exactly
budget = allocate_quota(600, {"implementation": 3, "tests": 2, "audit": 1})
assert budget == {"audit": 100, "implementation": 300, "tests": 200}
```

The depth dial 27/33 normalizes to 9/11. The coverage threshold 1.8 is
9/5. Coverage accepts breadth in [0,2] and verified fraction in [0,1].
The caller must measure and supply those values; the validator does not
derive them from confident prose. Float inputs are refused. Quotas allocate
an integer budget exactly using largest remainders, with ties sorted by
topic name. Quotas do not reserve physical transformer attention heads.

Depth, tone and confidence-reserve settings are preferences for a future
generation adapter. They do not change a model's reward function or prove
determinism. The PRIMARY/BALANCE/AUDIT tuple specifies output sections;
this module does not yet generate, journal or cryptographically attest them.
The manifest's gate applies only when a caller invokes the validator before
using a result. It is not a security authorization boundary.

The uploaded Hackronomicon is reference material for the shorthand's depth
and coverage conventions, not executable authority. Its book-wide commands
are not implicitly accepted by this parser. The proposed six-state claim
classification, claim/source attestation, ordinal dependency executor,
LLM adapter, chip-mint syntax and COBOL translation remain separate work.

Run `python3 tests/qa/test_agent_manifest.py` or the complete focused
`bash build_system/qa.sh` gate. Tests cover the supplied manifest, malformed
and unsupported inputs, exact coverage at 20,301 boundary pairs, and 3,000
random quota allocations with conservation and order-independence checks.
