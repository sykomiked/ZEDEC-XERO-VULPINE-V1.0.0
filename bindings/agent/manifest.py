# SPDX-License-Identifier: Apache-2.0
"""Bounded shorthand manifest data, not an LLM activation or execution API."""
from dataclasses import dataclass
from fractions import Fraction
import re

MAX_SOURCE_BYTES = 4096
MAX_INTEGER = (1 << 63) - 1
NUMBER = r"[0-9]+(?:/[0-9]+|\.[0-9]+)?"


def rational(value):
    """Accept exact integers, fractions or bounded textual decimal/ratio values."""
    if isinstance(value, bool) or isinstance(value, float):
        raise ValueError("use an exact rational, not a floating-point value")
    if isinstance(value, str):
        if len(value) > 64 or not re.fullmatch(NUMBER, value):
            raise ValueError("invalid rational literal")
        try:
            value = Fraction(value)
        except (ValueError, ZeroDivisionError) as exc:
            raise ValueError("invalid rational literal") from exc
    elif isinstance(value, int):
        value = Fraction(value)
    if not isinstance(value, Fraction):
        raise ValueError("unsupported rational representation")
    if abs(value.numerator) > MAX_INTEGER or value.denominator > MAX_INTEGER:
        raise ValueError("rational exceeds the C core's normalized integer bounds")
    return value


@dataclass(frozen=True)
class AgentManifest:
    depth: Fraction
    coverage_threshold: Fraction
    confidence_reserve: Fraction
    tone: tuple
    corridor: str
    action: str = "CORE_AUDIT"
    legend: str = "M5"
    output: tuple = ("PRIMARY", "BALANCE", "AUDIT")

    def __post_init__(self):
        # Direct construction has the same boundary as parsed manifests.
        for field in ("depth", "coverage_threshold", "confidence_reserve"):
            object.__setattr__(self, field, rational(getattr(self, field)))
        if not 0 <= self.depth <= 3:
            raise ValueError("depth must be in [0, 3]")
        if not 0 < self.coverage_threshold <= 2:
            raise ValueError("coverage threshold must be in (0, 2]")
        if not 0 <= self.confidence_reserve <= 1:
            raise ValueError("reserve must be in [0, 1]")
        if self.action != "CORE_AUDIT" or self.legend != "M5":
            raise ValueError("unsupported action or legend")
        if not isinstance(self.output, (tuple, list)) or tuple(self.output) != ("PRIMARY", "BALANCE", "AUDIT"):
            raise ValueError("unsupported output schema")
        object.__setattr__(self, "output", tuple(self.output))
        if not isinstance(self.corridor, str) or not re.fullmatch(r"[A-Z0-9_-]{1,64}", self.corridor):
            raise ValueError("invalid corridor identifier")
        tones = []
        seen = set()
        if not isinstance(self.tone, (tuple, list)) or len(self.tone) > 5:
            raise ValueError("invalid or oversized tone list")
        for name, value in self.tone:
            if name not in ("🎯", "🛡️", "⚖️", "jargon", "output_emoji") or name in seen:
                raise ValueError("unknown or duplicate tone modifier")
            value = rational(value)
            if not 0 <= value <= 3:
                raise ValueError("tone modifier must be in [0, 3]")
            seen.add(name)
            tones.append((name, value))
        object.__setattr__(self, "tone", tuple(tones))


def parse_manifest(source):
    if not isinstance(source, str) or len(source) > MAX_SOURCE_BYTES:
        raise ValueError("manifest exceeds size limit")
    if len(source.encode("utf-8")) > MAX_SOURCE_BYTES or "\x00" in source:
        raise ValueError("invalid manifest encoding or size")
    lines = [line.strip() for line in source.splitlines() if line.strip()]
    if len(lines) != 8 or lines[0] != "@SYSTEM-EXEC : CORE_AUDIT":
        raise ValueError("unsupported manifest shape")
    fields = {}
    for line in lines[1:7]:
        match = re.fullmatch(r"([A-Z]+)\s*:\s*(.+)", line)
        if not match or match[1] in fields:
            raise ValueError("invalid or duplicate clause")
        fields[match[1]] = match[2]
    if set(fields) != {"INVOKE", "DIAL", "GATE", "RESERVE", "TONE", "OUTPUT"}:
        raise ValueError("unknown or missing clause")
    if fields["INVOKE"] != "BIND_LEGEND(M5)":
        raise ValueError("unsupported legend")
    if fields["OUTPUT"] != "TRIPLE_LEDGER(PRIMARY, BALANCE, AUDIT)":
        raise ValueError("unsupported output schema")
    depth = re.fullmatch(r"\^\((" + NUMBER + r")\)", fields["DIAL"])
    gate = re.fullmatch(r"r\*l\s*>=\s*(" + NUMBER + r")", fields["GATE"])
    reserve = re.fullmatch(r"(" + NUMBER + r")\(CONFIDENCE\)", fields["RESERVE"])
    dispatch = re.fullmatch(r'@DISPATCH\s*:\s*EVAL_CORRIDOR_SOLVENCY\("([A-Z0-9_-]{1,64})"\)', lines[-1])
    if not all((depth, gate, reserve, dispatch)):
        raise ValueError("unsupported dial, gate, reserve or dispatch")
    tone = fields["TONE"]
    if not tone.startswith("[") or not tone.endswith("]"):
        raise ValueError("invalid tone list")
    tones = []
    for item in tone[1:-1].split(","):
        match = re.fullmatch(r"(🎯|🛡️|⚖️|jargon|output_emoji)\^\((" + NUMBER + r")\)", item.strip())
        if not match:
            raise ValueError("unknown tone expression")
        tones.append((match[1], match[2]))
    return AgentManifest(depth[1], gate[1], reserve[1], tones, dispatch[1])


@dataclass(frozen=True)
class CoverageResult:
    breadth: Fraction
    verified: Fraction
    product: Fraction
    threshold: Fraction
    allowed: bool


def evaluate_coverage(manifest, breadth, verified):
    """Validate caller-supplied measurements; this does not measure evidence."""
    breadth, verified = rational(breadth), rational(verified)
    if not 0 <= breadth <= 2 or not 0 <= verified <= 1:
        raise ValueError("coverage measurements outside their defined ranges")
    product = breadth * verified
    return CoverageResult(breadth, verified, product, manifest.coverage_threshold,
                          product >= manifest.coverage_threshold)


def allocate_quota(total, weights):
    """Exact largest-remainder integer allocation; ties sort by topic name."""
    if isinstance(total, bool) or not isinstance(total, int) or not 0 <= total <= MAX_INTEGER:
        raise ValueError("invalid integer budget")
    if not isinstance(weights, dict) or not 1 <= len(weights) <= 64:
        raise ValueError("quota needs between one and 64 topics")
    for name in weights:
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", name):
            raise ValueError("invalid topic name")
    values = {name: rational(value) for name, value in weights.items()}
    if any(value < 0 for value in values.values()) or sum(values.values()) <= 0:
        raise ValueError("quota weights must be nonnegative with positive sum")
    denominator = sum(values.values())
    exact = {name: total * value / denominator for name, value in values.items()}
    result = {name: value.numerator // value.denominator for name, value in exact.items()}
    remainder = total - sum(result.values())
    order = sorted(values, key=lambda name: (-(exact[name] - result[name]), name))
    for name in order[:remainder]:
        result[name] += 1
    return {name: result[name] for name in sorted(result)}
