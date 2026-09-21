#!/usr/bin/env python3
# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import importlib
import os
from pathlib import Path
import sys

from ts_ci import log

sys.path.insert(1, Path(__file__).parents[1].as_posix())
from ci.common import run_tests, TestConfig

TESTS_DIR = Path(__file__).parent / "examples"
TESTS_LIST = sorted(
    {
        e.removesuffix(".py")
        for e in os.listdir(TESTS_DIR)
        if (e.endswith(".py") and e != "__init__.py")
    }
)

if __name__ == "__main__":
    if len(TESTS_LIST) == 0:
        log.error("no tests found")
        exit(1)

    test_cases: list[TestConfig] = []

    for example in TESTS_LIST:
        mod = importlib.import_module(f"ci.examples.{example}")
        test_cases.extend(mod.TEST_CASES)

    run_tests(test_cases)
