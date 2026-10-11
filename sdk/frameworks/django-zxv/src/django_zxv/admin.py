# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""An advisory, read-only admin page: does Assets = Liabilities + Equity hold?"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any, Generic, Optional, TypeVar

from django.contrib import admin
from django.core.exceptions import PermissionDenied
from django.db import models
from django.http import HttpRequest, HttpResponse, HttpResponseNotAllowed
from django.template.response import TemplateResponse
from django.urls import URLPattern, path

from .ledger import accounting_equation, overall

_M = TypeVar("_M", bound=models.Model)


if TYPE_CHECKING:
    _AdminBase = admin.ModelAdmin
else:  # ModelAdmin is generic only in django-stubs

    class _AdminBase(admin.ModelAdmin, Generic[_M]):
        pass


class LedgerEquationAdmin(_AdminBase[_M]):
    """A ``ModelAdmin`` with a read-only "Accounting equation" page for ledger lines.

    Set the three field names (``equation_currency_field`` may be ``None``):

    .. code-block:: python

        @admin.register(LedgerLine)
        class LedgerLineAdmin(LedgerEquationAdmin[LedgerLine]):
            equation_kind_field = "kind"          # "asset" | "liability" | "equity"
            equation_amount_field = "amount"      # a RationalField
            equation_currency_field = "currency"

    The page (``<changelist>/accounting-equation/``) needs view permission,
    answers GET only, runs one SELECT over ``get_queryset(request)``, and
    shows exact totals per currency with a TRUE / FALSE / NEITHER verdict.
    It is advisory: it never blocks a save and never writes.
    """

    equation_kind_field: str = "kind"
    equation_amount_field: str = "amount"
    equation_currency_field: Optional[str] = "currency"
    change_list_template = "django_zxv/change_list_with_equation.html"

    def _equation_url_name(self) -> str:
        return f"{self.opts.app_label}_{self.opts.model_name}_accounting_equation"

    def get_urls(self) -> list[URLPattern]:
        own = [path("accounting-equation/", self.admin_site.admin_view(self.accounting_equation_view),
                    name=self._equation_url_name())]
        return own + super().get_urls()

    def changelist_view(self, request: HttpRequest, extra_context: Optional[dict[str, Any]] = None) -> HttpResponse:
        extra = {"zxv_equation_url_name": f"{self.admin_site.name}:{self._equation_url_name()}"}
        extra.update(extra_context or {})
        return super().changelist_view(request, extra)

    def accounting_equation_view(self, request: HttpRequest) -> HttpResponse:
        """The read-only page."""
        if request.method not in ("GET", "HEAD"):
            return HttpResponseNotAllowed(["GET", "HEAD"])
        if not self.has_view_permission(request):
            raise PermissionDenied
        fields = [self.equation_kind_field, self.equation_amount_field]
        if self.equation_currency_field:
            fields.append(self.equation_currency_field)
        rows = self.get_queryset(request).order_by().values_list(*fields)
        lines = ((r[0], r[1], r[2] if len(r) > 2 else None) for r in rows)
        results = accounting_equation(lines)
        context = {
            **self.admin_site.each_context(request),
            "opts": self.opts,
            "title": "Accounting equation (advisory)",
            "results": [r.as_text() | {"lines": r.lines, "unclassified": r.unclassified} for r in results],
            "verdict": overall(results).name,
        }
        return TemplateResponse(request, "django_zxv/accounting_equation.html", context)
