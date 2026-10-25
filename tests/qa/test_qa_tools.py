# SPDX-License-Identifier: Apache-2.0
"""Negative tests ensure QA tooling cannot report missing evidence as success."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py')
    loaded=importlib.util.module_from_spec(spec);spec.loader.exec_module(loaded)
    return loaded

class Inventory(unittest.TestCase):
    def test_component_makefile_uses_component_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            subprocess.run(['git', 'init', '-q', folder], check=True)
            component = root / 'bindings/dotnet/native'
            component.mkdir(parents=True)
            (component / 'Makefile').write_text('test: test_api.c test_missing.c\n')
            (component / 'test_api.c').write_text('int main(void) { return 0; }\n')
            report = module('qa_inventory').inventory(root)
            self.assertEqual(set(report['missing_test_inputs']),
                             {'bindings/dotnet/native/test_missing.c'})

    def test_missing_input_and_untracked_source(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            subprocess.run(['git','init','-q',folder],check=True)
            (root/'kernel').mkdir()
            (root/'kernel/Makefile').write_text('test: src/test_absent.c ../tests/test_absent.c\n')
            (root/'new.c').write_text('int new_feature(void) { return 1; }\n')
            report=module('qa_inventory').inventory(root)
            self.assertEqual(report['c_sources'],1)
            self.assertEqual(set(report['missing_test_inputs']),{'kernel/src/test_absent.c','tests/test_absent.c'})
            self.assertEqual(report['test_code_budget']['actual_ratio'],0)

class Boot(unittest.TestCase):
    def test_all_architecture_fault_markers_fail(self):
        for marker in ('CPU exception vector=', 'PANIC', 'Kernel panic', 'kernel panic'):
            with self.subTest(marker=marker):
                passed, _ = module('qa_boot_matrix').boot(
                    [sys.executable, '-c', f'print("[BOOT_OK] {marker}", flush=True)'], 1)
                self.assertFalse(passed)

    def test_extra_required_marker_cannot_be_omitted(self):
        passed, _ = module('qa_boot_matrix').boot(
            [sys.executable, '-c', 'import time; print("[BOOT_OK]", flush=True); time.sleep(1)'],
            0.2, required_markers=(b'[BOOT_OK]', b'[AI_OK]'))
        self.assertFalse(passed)

    def test_process_exit_is_failure(self):
        passed,_=module('qa_boot_matrix').boot([sys.executable,'-c','print("no milestone")'],1)
        self.assertFalse(passed)
    def test_fault_with_marker_is_failure(self):
        passed,_=module('qa_boot_matrix').boot([sys.executable,'-c','print("[BOOT_OK] [FAULT]", flush=True)'],1)
        self.assertFalse(passed)
    def test_fault_after_marker_is_failure(self):
        passed,_=module('qa_boot_matrix').boot([sys.executable,'-c',
            'import time; print("[BOOT_OK]", flush=True); time.sleep(0.1); print("[FAULT]", flush=True)'],1)
        self.assertFalse(passed)
    def test_success_needs_running_process(self):
        passed,_=module('qa_boot_matrix').boot([sys.executable,'-c',
            'import time; print("[BOOT_OK]", flush=True); time.sleep(5)'],3)
        self.assertTrue(passed)
    def test_marker_then_exit_is_failure(self):
        passed,_=module('qa_boot_matrix').boot([sys.executable,'-c','print("[BOOT_OK]", flush=True)'],1)
        self.assertFalse(passed)
    def test_timeout_is_failure(self):
        passed,_=module('qa_boot_matrix').boot([sys.executable,'-c','import time; time.sleep(1)'],0.1)
        self.assertFalse(passed)

if __name__ == '__main__':
    unittest.main()
