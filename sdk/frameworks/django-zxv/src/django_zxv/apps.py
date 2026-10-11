# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The app config: owns the ZXV context for the life of the process."""

from __future__ import annotations

from django.apps import AppConfig
from django.conf import settings
from django.core.exceptions import ImproperlyConfigured

from . import runtime
from .conf import ZxvSettingsError, parse_settings


class ZxvConfig(AppConfig):
    """Creates the context when Django starts, so a bad ``ZXV`` setting or a
    missing ``libzxv`` fails start-up (``ImproperlyConfigured``), not the
    first request. The context is closed in ABI.md R6 order at interpreter
    exit, or earlier with :func:`django_zxv.shutdown`.
    """

    name = "django_zxv"
    label = "django_zxv"
    verbose_name = "ZXV"
    default_auto_field = "django.db.models.BigAutoField"

    def ready(self) -> None:
        from . import checks  # noqa: F401  (registers the system checks)

        try:
            parsed = parse_settings(getattr(settings, "ZXV", None))
        except ZxvSettingsError as e:
            raise ImproperlyConfigured(str(e)) from e
        runtime.install(parsed)
