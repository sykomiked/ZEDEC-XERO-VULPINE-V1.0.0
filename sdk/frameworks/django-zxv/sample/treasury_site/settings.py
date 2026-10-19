# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Settings of the sample project (local demo only: DEBUG, a fixed dev secret, SQLite)."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any

BASE_DIR = Path(__file__).resolve().parent.parent
_REPO_LIB = BASE_DIR.parents[2] / "libzxv" / "build"  # sdk/libzxv/build when run from this repository

SECRET_KEY = os.environ.get("DJANGO_SECRET_KEY", "dev-only-not-secret")
DEBUG = os.environ.get("DJANGO_DEBUG", "1") == "1"
ALLOWED_HOSTS = ["localhost", "127.0.0.1", "testserver"]

INSTALLED_APPS = [
    "django.contrib.admin",
    "django.contrib.auth",
    "django.contrib.contenttypes",
    "django.contrib.sessions",
    "django.contrib.messages",
    "django.contrib.staticfiles",
    "django_zxv",
    "treasury",
]

MIDDLEWARE = [
    "django_zxv.problems.ZxvProblemMiddleware",  # first: a halted context is answered 503
    "django.middleware.security.SecurityMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "django.middleware.common.CommonMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "django.contrib.messages.middleware.MessageMiddleware",
    "django.middleware.clickjacking.XFrameOptionsMiddleware",
]

ROOT_URLCONF = "treasury_site.urls"
WSGI_APPLICATION = "treasury_site.wsgi.application"

TEMPLATES: list[dict[str, Any]] = [
    {
        "BACKEND": "django.template.backends.django.DjangoTemplates",
        "DIRS": [],
        "APP_DIRS": True,
        "OPTIONS": {
            "context_processors": [
                "django.template.context_processors.request",
                "django.contrib.auth.context_processors.auth",
                "django.contrib.messages.context_processors.messages",
            ],
        },
    },
]

DATABASES = {"default": {"ENGINE": "django.db.backends.sqlite3", "NAME": BASE_DIR / "db.sqlite3"}}
DEFAULT_AUTO_FIELD = "django.db.models.BigAutoField"
STATIC_URL = "static/"
USE_TZ = True

ZXV: dict[str, Any] = {
    "CURRENCIES": ["NGN"],
    "ROOT_RIGHTS": ["LEDGER_READ", "LEDGER_POST"],
    "CAPABILITIES": {
        "teller": ["LEDGER_READ", "LEDGER_POST"],
        "auditor": "LEDGER_READ",
    },
    "LIBRARY_PATH": os.environ.get("ZXV_LIBRARY_PATH") or (str(_REPO_LIB) if _REPO_LIB.is_dir() else None),
}

# POST /admin-halt halts the context. Local demos and tests only.
SAMPLE_HALT_ENDPOINT = os.environ.get("ZXV_SAMPLE_HALT") == "1"
