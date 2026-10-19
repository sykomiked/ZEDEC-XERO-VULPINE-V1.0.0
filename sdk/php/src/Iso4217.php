<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** The ISO 4217 table compiled into libzxv (process-wide, read-only). */
final class Iso4217
{
    private function __construct()
    {
    }

    /** Look up by alpha-3 ("NGN"); null if not in ISO 4217 (VFV included). */
    public static function lookup(string $alpha): ?Currency
    {
        $num = Native::new("uint16_t");
        $minor = Native::new("uint8_t");
        $flags = Native::new("uint32_t");
        $rc = Native::ffi()->zxv_iso4217_by_alpha($alpha, \FFI::addr($num), \FFI::addr($minor), \FFI::addr($flags));
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        Native::check($rc);
        return new Currency($alpha, $minor->cdata, $flags->cdata, $num->cdata);
    }

    /** Look up by numeric (566 -> NGN); null if unknown (555/777/888 included). */
    public static function byNumeric(int $numeric): ?Currency
    {
        $n = Native::int($numeric, "numeric", 0, 65535);
        $minor = Native::new("uint8_t");
        $flags = Native::new("uint32_t");
        [$rc, $alpha] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_iso4217_by_numeric($n, $b, $c, $l, \FFI::addr($minor), \FFI::addr($flags)));
        if ($rc === Status::E_NOT_FOUND) {
            return null;
        }
        Native::check($rc);
        return new Currency((string) $alpha, $minor->cdata, $flags->cdata, $n);
    }

    /** English name ("Naira"). */
    public static function name(string $alpha): string
    {
        [$rc, $s] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_iso4217_name($alpha, $b, $c, $l));
        Native::check($rc);
        return (string) $s;
    }

    /** @return list<string> every alpha code in the table */
    public static function codes(): array
    {
        $n = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_iso4217_count(\FFI::addr($n)));
        $out = [];
        for ($i = 0; $i < $n->cdata; $i++) {
            [$rc, $s] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_iso4217_at($i, $b, $c, $l));
            Native::check($rc);
            $out[] = (string) $s;
        }
        return $out;
    }

    /** Publication date of the compiled list ("2026-01-01"). */
    public static function published(): string
    {
        [$rc, $s] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_iso4217_published($b, $c, $l));
        Native::check($rc);
        return (string) $s;
    }

    /** Is an ISO 3166 alpha-2 country an African Union member? */
    public static function isAfricanUnionMember(string $country): bool
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_au_is_member($country, \FFI::addr($out)));
        return $out->cdata === 1;
    }
}
