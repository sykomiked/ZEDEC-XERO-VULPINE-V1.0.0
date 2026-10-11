# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""URLs of the sample."""

from django.contrib import admin
from django.urls import URLPattern, URLResolver, include, path

from django_zxv.views import health

urlpatterns: list[URLPattern | URLResolver] = [
    path("admin/", admin.site.urls),
    path("health/", health, name="health"),
    path("", include("treasury.urls")),
]
