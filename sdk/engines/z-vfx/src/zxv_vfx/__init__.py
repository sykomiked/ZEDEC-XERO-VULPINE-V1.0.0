# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Z-VFX: exact frame times, transforms, procedural seeds and render-farm
budgets for VFX pipelines, with thin Maya and Houdini adapters.

The core (this package's top level) never uses a float: frame rates and
frame times are rationals, transforms are rational matrices, seeds come
from SHA3-256 and budgets from libzxv's rational ``rmag``. The DCC
adapters, :mod:`zxv_vfx.maya` and :mod:`zxv_vfx.houdini`, are imported on
their own and convert to floats only when writing into the DCC.
"""

from __future__ import annotations

from .farm import FarmBudget, JobRefused, RenderJob, Settlement
from .frametime import (FILM, FILM_NTSC, HFR_60, NTSC, NTSC_30, NTSC_FIELD, PAL, PAL_FIELD, SHOW,
                        FrameRange, FrameRate, FrameTime, Timecode)
from .seeds import DOMAIN, SeedScheme, SeedStream, encode_part
from .transform import Matrix4, Vec3

__version__ = "0.1.0"

__all__ = [
    "__version__",
    "FrameRate", "FrameTime", "FrameRange", "Timecode",
    "FILM", "FILM_NTSC", "PAL", "NTSC", "NTSC_30", "SHOW", "PAL_FIELD", "NTSC_FIELD", "HFR_60",
    "Vec3", "Matrix4",
    "SeedScheme", "SeedStream", "encode_part", "DOMAIN",
    "FarmBudget", "RenderJob", "JobRefused", "Settlement",
]
