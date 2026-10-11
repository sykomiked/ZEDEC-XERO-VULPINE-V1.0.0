# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
from django.urls import URLPattern, path

from . import views

urlpatterns: list[URLPattern] = [
    path("quote", views.quote),
    path("sum", views.sum_view),
    path("payments", views.payments),
    path("refunds", views.refunds),
    path("balance", views.balance),
    path("admin-halt", views.admin_halt),
]
