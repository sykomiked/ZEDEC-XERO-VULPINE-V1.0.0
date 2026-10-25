# SPDX-License-Identifier: Apache-2.0
"""Independent boundary and allocation properties for the SDK control data."""
from dataclasses import FrozenInstanceError, replace
from fractions import Fraction
from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from bindings.agent.manifest import (AgentManifest, MAX_SOURCE_BYTES, allocate_quota,
                                     evaluate_coverage, parse_manifest, rational)

SOURCE = '''@SYSTEM-EXEC : CORE_AUDIT
  INVOKE : BIND_LEGEND(M5)
  DIAL : ^(27/33)
  GATE : r*l >= 1.8
  RESERVE : 1/5(CONFIDENCE)
  TONE : [ 🎯^(2), 🛡️^(1), jargon^(0), output_emoji^(0) ]
  OUTPUT : TRIPLE_LEDGER(PRIMARY, BALANCE, AUDIT)
@DISPATCH : EVAL_CORRIDOR_SOLVENCY("LAGOS-MOMBASA")
'''


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.policy = parse_manifest(SOURCE)

    def test_supplied_manifest_has_exact_normalized_values(self):
        self.assertEqual(self.policy.depth, Fraction(9, 11))
        self.assertEqual(self.policy.coverage_threshold, Fraction(9, 5))
        self.assertEqual(self.policy.confidence_reserve, Fraction(1, 5))
        self.assertEqual(self.policy.corridor, 'LAGOS-MOMBASA')
        self.assertEqual(self.policy.output, ('PRIMARY', 'BALANCE', 'AUDIT'))
        self.assertEqual(dict(self.policy.tone)['jargon'], 0)
        with self.assertRaises(FrozenInstanceError):
            self.policy.depth = Fraction(2)

    def test_mutable_input_cannot_change_the_policy(self):
        tone = [['jargon', 0]]
        output = ['PRIMARY', 'BALANCE', 'AUDIT']
        policy = AgentManifest('27/33', '1.8', '1/5', tone, 'A-B', output=output)
        tone[0][1] = 3
        output[0] = 'UNSUPPORTED'
        self.assertEqual(policy.tone, (('jargon', Fraction(0)),))
        self.assertEqual(policy.output, ('PRIMARY', 'BALANCE', 'AUDIT'))

    def test_unknown_missing_duplicate_and_executable_clauses_rejected(self):
        invalid = [SOURCE + 'RUN shell\n', SOURCE.replace('CORE_AUDIT', 'SETTLE'),
                   SOURCE.replace('BIND_LEGEND(M5)', 'BIND_LEGEND(UNKNOWN)'),
                   SOURCE.replace('DIAL : ^(27/33)', 'GATE : r*l >= 1.8'),
                   SOURCE.replace('DIAL : ^(27/33)', 'UNKNOWN : ^(27/33)'),
                   SOURCE.replace('  RESERVE : 1/5(CONFIDENCE)\n', ''),
                   SOURCE.replace('r*l >= 1.8', 'r*l > 1.8'),
                   SOURCE.replace('LAGOS-MOMBASA', '../arbitrary/path'),
                   SOURCE.replace('LAGOS-MOMBASA', 'A' * 65),
                   SOURCE.replace('PRIMARY, BALANCE, AUDIT', 'PRIMARY, BALANCE'),
                   SOURCE.replace('🛡️^(1)', 'UNKNOWN^(1)'),
                   SOURCE.replace('🛡️^(1)', '🎯^(1)'),
                   SOURCE.replace('^(27/33)', '^(27/0)'),
                   SOURCE.replace('^(27/33)', '^(-1)'),
                   SOURCE.replace('^(27/33)', '^(4)'),
                   SOURCE.replace('1/5(CONFIDENCE)', '2(CONFIDENCE)'),
                   SOURCE.replace('1.8', '0'),
                   SOURCE.replace('1.8', '2.1'),
                   SOURCE + '\x00', 'x' * (MAX_SOURCE_BYTES + 1),
                   '🎯' * MAX_SOURCE_BYTES]
        for source in invalid:
            with self.subTest(source=source[:70]), self.assertRaises(ValueError):
                parse_manifest(source)

    def test_direct_construction_cannot_bypass_bounds(self):
        for changes in ({'depth': -1}, {'depth': 4}, {'depth': 0.5},
                        {'coverage_threshold': 0}, {'coverage_threshold': 3},
                        {'confidence_reserve': -1}, {'confidence_reserve': 2},
                        {'tone': [('unknown', 0)]}, {'tone': [('jargon', 4)]},
                        {'tone': [('jargon', 0)] * 6}, {'tone': iter([])},
                        {'output': None}, {'action': 'SETTLE'}, {'corridor': 'a/b'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                replace(self.policy, **changes)

    def test_exact_literals_and_invalid_representations(self):
        for text, expected in [('27/33', Fraction(9, 11)), ('1.8', Fraction(9, 5)),
                               ('8/9000', Fraction(1, 1125)), ('0', Fraction(0))]:
            self.assertEqual(rational(text), expected)
        for value in [True, False, 1.8, float('nan'), float('inf'), None,
                      '', '1/0', '1e3', '-1', '1 / 2', '1.2.3', '1/' + '1' * 65,
                      1 << 63, Fraction(1, 1 << 63)]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                rational(value)

    def test_coverage_uses_integer_inequality_at_every_hundredth(self):
        for breadth in range(201):
            for verified in range(101):
                result = evaluate_coverage(self.policy, Fraction(breadth, 100),
                                           Fraction(verified, 100))
                # Independent scaled-integer check; never a float comparison.
                self.assertEqual(result.allowed, breadth * verified >= 18000)
        self.assertTrue(evaluate_coverage(self.policy, 2, '0.9').allowed)
        self.assertFalse(evaluate_coverage(self.policy, 2, '0.899999999').allowed)
        self.assertTrue(evaluate_coverage(self.policy, '1.9', '0.97').allowed)
        self.assertFalse(evaluate_coverage(self.policy, 2, '0.6').allowed)

    def test_invalid_measurements_do_not_get_a_result(self):
        for breadth, verified in [(-1, 1), (3, 1), (2, -1), (2, 2), (1.8, 1), (2, True)]:
            with self.subTest(breadth=breadth, verified=verified), self.assertRaises(ValueError):
                evaluate_coverage(self.policy, breadth, verified)

    def test_quota_known_values_and_ties(self):
        self.assertEqual(allocate_quota(6, {'A': 3, 'B': 2, 'C': 1}), {'A': 3, 'B': 2, 'C': 1})
        self.assertEqual(allocate_quota(1, {'B': 1, 'A': 1}), {'A': 1, 'B': 0})
        self.assertEqual(allocate_quota(0, {'A': 1}), {'A': 0})
        self.assertEqual(allocate_quota(10, {'A': 0, 'B': 1}), {'A': 0, 'B': 10})

    def test_quota_conservation_bounds_and_order_independence(self):
        rng = random.Random(0x889F)
        for _ in range(3000):
            total = rng.randrange(10000)
            weights = {f'T{i}': Fraction(rng.randrange(20), rng.randrange(1, 100))
                       for i in range(rng.randrange(1, 12))}
            weights['positive'] = Fraction(1, 3)
            allocation = allocate_quota(total, weights)
            self.assertEqual(sum(allocation.values()), total)
            self.assertEqual(set(allocation), set(weights))
            for name, amount in allocation.items():
                self.assertGreaterEqual(amount, 0)
                ideal = total * weights[name] / sum(weights.values())
                self.assertLess(abs(amount - ideal), 1)
                if weights[name] == 0:
                    self.assertEqual(amount, 0)
            reversed_weights = dict(reversed(list(weights.items())))
            self.assertEqual(allocation, allocate_quota(total, reversed_weights))

    def test_invalid_quotas(self):
        for total, weights in [(-1, {'A': 1}), (1.0, {'A': 1}), (True, {'A': 1}),
                               (1 << 63, {'A': 1}), (1, {}), (1, {'A': 0}),
                               (1, {'A': -1}), (1, {'A': 0.5}),
                               (1, {'bad name': 1}), (1, {str(i): 1 for i in range(65)})]:
            with self.subTest(total=total, weights=weights), self.assertRaises(ValueError):
                allocate_quota(total, weights)


if __name__ == '__main__':
    unittest.main()
