#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Runs the tests with Django's test runner: python runtests.py [-v 2] [tests.test_fields ...]"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("labels", nargs="*", default=["tests"])
    parser.add_argument("-v", "--verbosity", type=int, default=1)
    args = parser.parse_args()

    sys.path[:0] = [str(HERE), str(HERE / "sample")]
    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "tests.settings")
    os.environ.setdefault("ZXV_LIBRARY_PATH", str(HERE.parents[1] / "libzxv" / "build"))

    import django
    from django.conf import settings
    from django.test.utils import get_runner

    django.setup()
    runner = get_runner(settings)(verbosity=args.verbosity)
    failures = runner.run_tests(args.labels)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
