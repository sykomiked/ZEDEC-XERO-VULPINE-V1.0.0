# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Test settings: the sample project's, with an in-memory database and the halt endpoint."""

from treasury_site.settings import *  # noqa: F403

DATABASES = {"default": {"ENGINE": "django.db.backends.sqlite3", "NAME": ":memory:"}}
ROOT_URLCONF = "tests.urls"
SAMPLE_HALT_ENDPOINT = True
PASSWORD_HASHERS = ["django.contrib.auth.hashers.MD5PasswordHasher"]
LOGGING = {"version": 1, "disable_existing_loggers": False, "loggers": {"django_zxv": {"handlers": [], "propagate": False}, "django.request": {"handlers": [], "propagate": False}}}
