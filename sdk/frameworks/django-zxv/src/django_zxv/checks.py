# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""System checks: ``manage.py check`` reports an invalid ``ZXV`` setting."""

from __future__ import annotations

from typing import Any, Optional, Sequence

from django.apps import AppConfig
from django.conf import settings
from django.core.checks import CheckMessage, Error, register

from .conf import ZxvSettingsError, parse_settings


@register("zxv")
def check_settings(app_configs: Optional[Sequence[AppConfig]] = None, **kwargs: Any) -> list[CheckMessage]:
    """``django_zxv.E001``: one error per problem in ``settings.ZXV``."""
    try:
        parse_settings(getattr(settings, "ZXV", None))
    except ZxvSettingsError as e:
        return [Error(p, hint="See the django-zxv README, section Settings.", id="django_zxv.E001") for p in e.problems]
    return []
