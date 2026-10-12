# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The render-farm budget on libzxv's rational rmag budgets."""

from __future__ import annotations

import pytest
import zxv
from zxv import Rational

from zxv_vfx import FILM, FILM_NTSC, FarmBudget, FrameRange, JobRefused, RenderJob


def job(name: str, start: int, end: int, cost: str) -> RenderJob:
    return RenderJob(name, FrameRange(start, end, FILM), cost)


def test_job_cost() -> None:
    j = job("sh010_beauty", 1001, 1100, "1/3")  # 20 core-minutes a frame
    assert j.cost == Rational(100, 3)
    assert RenderJob("x", FrameRange(1, 10, FILM_NTSC, step=2), "2.5").cost == Rational(25, 2)
    with pytest.raises(zxv.FloatRejectedError):
        RenderJob("x", FrameRange(1, 2, FILM), 0.5)
    with pytest.raises(ValueError):
        RenderJob("x", FrameRange(1, 2, FILM), 0)
    with pytest.raises(ValueError):
        RenderJob("", FrameRange(1, 2, FILM), 1)
    with pytest.raises(TypeError):
        RenderJob("x", (1, 2), 1)  # type: ignore[arg-type]


def test_submit_and_refuse(cap: zxv.Capability) -> None:
    with FarmBudget(cap, 100) as farm:
        assert farm.remaining == 100 and farm.used == 0
        a = job("a", 1, 90, "1/3")    # 30
        b = job("b", 1, 120, "1/2")   # 60
        c = job("c", 1, 31, "1/3")    # 31/3: does not fit after a + b
        assert farm.submit(a) == 30 and farm.submit(b) == 60
        assert farm.remaining == 10 and farm.jobs == ["a", "b"]
        with pytest.raises(JobRefused) as e:
            farm.submit(c)
        assert e.value.needed == Rational(31, 3) and e.value.remaining == 10
        assert farm.remaining == 10, "a refused job reserves nothing"
        with pytest.raises(ValueError):
            farm.submit(a)  # name in use
        assert farm.submit(job("d", 1, 30, "1/3")) == 10
        assert farm.remaining == 0


def test_thirds_never_drift(cap: zxv.Capability) -> None:
    # 3,000 jobs of one frame at 1/3 core-hour fill a 1,000 core-hour farm
    # exactly; a float total would be off by now.
    with FarmBudget(cap, 1000) as farm:
        for i in range(3000):
            farm.submit(job(f"j{i}", 1, 1, "1/3"))
        assert farm.remaining == 0 and farm.used == 1000
        with pytest.raises(JobRefused):
            farm.submit(job("one-more", 1, 1, "1/3"))
        for i in range(3000):
            farm.complete(f"j{i}", "1/6")
        assert farm.used == 500 and farm.remaining == 500


def test_settle(cap: zxv.Capability) -> None:
    with FarmBudget(cap, 50) as farm:
        farm.submit(job("a", 1, 10, 2))   # 20
        s = farm.complete("a", "17.5")
        assert (s.reserved, s.charged, s.returned, s.extra) == (20, Rational(35, 2), Rational(5, 2), 0)
        assert farm.used == Rational(35, 2)
        farm.submit(job("b", 1, 10, 2))   # 20 -> used 37.5
        s = farm.complete("b", 25)        # over-run of 5 -> used 42.5
        assert (s.returned, s.extra) == (0, 5) and farm.used == Rational(85, 2)
        farm.submit(job("c", 1, 2, 2))    # 4 -> used 46.5
        with pytest.raises(JobRefused):
            farm.complete("c", 10)        # 6 extra, only 3.5 left
        assert farm.jobs == ["c"] and farm.used == Rational(93, 2), "a refused over-run changes nothing"
        farm.raise_limit(60)
        s = farm.complete("c", 10)
        assert s.extra == 6 and farm.used == Rational(105, 2) and farm.jobs == []
        with pytest.raises(KeyError):
            farm.complete("c", 1)
        with pytest.raises(ValueError):
            farm.raise_limit(-1)


def test_cancel(cap: zxv.Capability) -> None:
    with FarmBudget(cap, "120.5") as farm:
        farm.submit(job("a", 1001, 1100, "1.2"))  # 120
        s = farm.cancel("a", frames_done=25)
        assert s.charged == 30 and s.returned == 90 and farm.used == 30
        farm.submit(job("b", 1, 10, 1))
        with pytest.raises(ValueError):
            farm.cancel("b", 11)
        assert farm.cancel("b").returned == 10 and farm.used == 30


def test_plan(cap: zxv.Capability) -> None:
    with FarmBudget(cap, "10.5") as farm:
        jobs = [job("a", 1, 6, 1), job("b", 1, 6, 1), job("c", 1, 4, 1), job("d", 1, 1, "1/2")]
        assert [j.name for j in farm.plan(jobs)] == ["a", "c", "d"]
        assert farm.used == 0, "plan reserves nothing"


def test_capability_needed(cap: zxv.Capability) -> None:
    weak = cap.attenuate(zxv.Cap.SWARM)
    with pytest.raises(zxv.CapabilityDeniedError):
        FarmBudget(weak, 10)
    with pytest.raises(zxv.FloatRejectedError):
        FarmBudget(cap, 10.0)
    with pytest.raises(ValueError):
        FarmBudget(cap, -1)
