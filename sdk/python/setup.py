# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Marks the wheel platform-specific when it bundles libzxv under zxv/_lib/.

All metadata lives in pyproject.toml. Without a bundled library the wheel
is pure Python (py3-none-any) and finds libzxv through ZXV_LIBRARY_PATH or
the system loader path.
"""

from pathlib import Path

from setuptools import setup
from setuptools.dist import Distribution

_LIB = Path(__file__).parent / "src" / "zxv" / "_lib"
_BUNDLED = any(_LIB.glob("*.so")) or any(_LIB.glob("*.dylib")) or any(_LIB.glob("*.dll"))

cmdclass = {}
if _BUNDLED:
    try:
        from setuptools.command.bdist_wheel import bdist_wheel
    except ImportError:  # older setuptools
        from wheel.bdist_wheel import bdist_wheel  # type: ignore[no-redef]

    class PlatformWheel(bdist_wheel):  # type: ignore[misc,valid-type]
        """A py3-none-<platform> wheel: Python code plus a native library."""

        def finalize_options(self) -> None:
            super().finalize_options()
            self.root_is_pure = False

        def get_tag(self):  # type: ignore[no-untyped-def]
            _, _, plat = super().get_tag()
            return "py3", "none", plat

    cmdclass["bdist_wheel"] = PlatformWheel


class _Dist(Distribution):
    def has_ext_modules(self) -> bool:  # noqa: D401
        return _BUNDLED


setup(cmdclass=cmdclass, distclass=_Dist)
