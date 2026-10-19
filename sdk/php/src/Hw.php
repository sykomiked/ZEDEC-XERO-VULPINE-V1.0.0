<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.4: hardware emulation and hardware updates (zxv_hw_*): constants,
 * release keys and signed packages. See sdk/docs/HW_EMULATION.md.
 *
 * - {@see HwProfile}: ZHP-1 machine profiles (plain data, no capability).
 * - {@see HwMatrix}: functions served by interchangeable, proven
 *   implementations; every matrix starts with the standard engines
 *   (FN_SHA256 ...). Needs Capability::HW_MATRIX (HW_RECONF for contract
 *   packages).
 * - {@see Hw::keygen()} / {@see Hw::buildImage()}: release keys and packages
 *   signed with ML-DSA-87 AND SLH-DSA-SHAKE-256s. Need CRYPTO_SIGN; SLH-DSA
 *   signing takes seconds (a release step).
 * - {@see HwRegion}: hardware updates as signed images. Needs HW_RECONF.
 *
 * Everything is an int or a byte string; floats are refused.
 */
final class Hw
{
    // Implementation kinds, in the order the matrix prefers them at equal cost.
    public const KIND_HARD = 0;
    public const KIND_FABRIC = 1;
    public const KIND_MICRO = 2;
    public const KIND_PEER = 3;
    public const KIND_SOFT = 4;
    public const KIND_REMOTE = 5;

    // Resource states.
    public const RES_OK = 0;
    public const RES_FAULTED = 1;
    public const RES_RECONFIGURING = 2;
    public const RES_OFFLINE = 3;
    /** The resource every matrix starts with: this machine's CPU. */
    public const RES_HOST = 0xFFFFFFF0;

    // The standard engines.
    public const FN_SHA256 = 0x5A450001;
    public const FN_KECCAK = 0x5A450002;
    public const FN_MAC = 0x5A450003;
    public const FN_GCD = 0x5A450004;
    public const FN_LOGIC64 = 0x5A450005;
    public const FN_TENSOR = 0x5A450006;

    // Placement policy flags.
    public const POLICY_KEY_BEARING = 0x1;
    public const POLICY_LOCAL_ONLY = 0x2;

    // Implementation status flags (HwProviderInfo::$flags).
    public const PF_FAULT = 0x01;
    public const PF_DISPUTED = 0x02;
    public const PF_FAILED = 0x04;
    public const PF_UNPROVEN = 0x08;
    public const PF_DISABLED = 0x10;
    public const PF_POLICY = 0x20;
    public const PF_SERVING = 0x40;

    // Coarse power classes.
    public const POWER_UNKNOWN = 0;
    public const POWER_10MW = 1;
    public const POWER_100MW = 2;
    public const POWER_1W = 3;
    public const POWER_10W = 4;
    public const POWER_100W = 5;
    public const POWER_MORE = 6;

    // Image and package kinds.
    public const IMG_BITSTREAM = 1;
    public const IMG_PARTIAL = 2;
    public const IMG_MICROCODE = 3;
    public const IMG_FIRMWARE = 4;
    public const IMG_VECTORS = 5;

    // Key and package sizes.
    public const PK_BYTES = 2656;
    public const MLDSA_SK_BYTES = 4896;
    public const SLHDSA_SK_BYTES = 128;
    public const SIG_BYTES = 34419;
    public const HEADER_BYTES = 96;

    // Region states.
    public const REGION_EMPTY = 0;
    public const REGION_ACTIVE = 1;
    public const REGION_STAGED = 2;
    public const REGION_PROBATION = 3;
    public const REGION_HALTED = 4;

    // Events and audit records.
    public const EV_STAGED = 1;
    public const EV_TESTED = 2;
    public const EV_ACTIVATED = 3;
    public const EV_ROLLED_BACK = 4;
    public const EV_REJECTED = 5;
    public const EV_QUARANTINED = 6;
    public const EV_CONTRACT = 7;

    // ZHP-1 component classes, capability keys and ISAs.
    public const CLASS_MACHINE = 1;
    public const CLASS_CPU_CLUSTER = 2;
    public const CLASS_CPU_CORE = 3;
    public const CLASS_CACHE = 4;
    public const CLASS_MEMORY = 5;
    public const CLASS_INTERCONNECT = 6;
    public const CLASS_MATRIX_UNIT = 7;
    public const CLASS_VECTOR_UNIT = 8;
    public const CLASS_TERNARY_UNIT = 9;
    public const CLASS_HASH_UNIT = 10;
    public const CLASS_CRYPTO_UNIT = 11;
    public const CLASS_CODEC_UNIT = 12;
    public const CLASS_RECONF_FABRIC = 13;
    public const CLASS_STORAGE = 14;
    public const CLASS_NETWORK = 15;
    public const CLASS_RADIO = 16;
    public const CLASS_SENSOR = 17;
    public const CLASS_ACTUATOR = 18;
    public const CLASS_CLOCK = 19;
    public const CLASS_POWER = 20;
    public const CLASS_SECURE_ELEMENT = 21;
    public const CLASS_DISPLAY = 22;
    public const CLASS_AUDIO = 23;
    public const CLASS_WIRE = 24;
    public const CLASS_GPU = 25;
    public const CLASS_PACKAGE = 26;
    public const CLASS_LANE = 27;
    public const CLASS_LOGIC_UNIT = 28;
    public const CLASS_NTT_UNIT = 29;
    public const CLASS_FRAME_UNIT = 30;
    public const CLASS_GCD_UNIT = 31;
    public const CLASS_MEM_CHANNEL = 32;
    public const CLASS_SECURE_ISLAND = 33;
    public const CLASS_IO = 34;
    public const CLASS_PRIVATE = 0x80000000;
    public const CAP_WORD_BITS = 0x0001;
    public const CAP_COUNT = 0x0002;
    public const CAP_FREQ_KHZ = 0x6003;
    public const CAP_BYTES = 0x0004;
    public const CAP_LINE_BYTES = 0x6005;
    public const CAP_ISA = 0x2006;
    public const CAP_INT_WIDTHS = 0x4007;
    public const CAP_RADIX = 0x2008;
    public const CAP_MACS_PER_CYCLE = 0x0009;
    public const CAP_SIMD = 0x400A;
    public const CAP_TICK_NS = 0x600B;
    public const CAP_REGIONS = 0x000C;
    public const CAP_BANDWIDTH_MBPS = 0x000D;
    public const CAP_LATENCY_NS = 0x600E;
    public const CAP_PQ_ALGS = 0x400F;
    public const CAP_HASH_ALGS = 0x4010;
    public const CAP_WIRE_BITS = 0x2011;
    public const CAP_PEAK_OPS_MHZ = 0x6012;
    public const CAP_CONTRACT_ID = 0x2013;
    public const CAP_CONTRACT_VER = 0x0014;
    public const CAP_KIND = 0x6015;
    public const CAP_CAPACITY_CLASS = 0x0016;
    public const CAP_PERF_BUCKET = 0x0017;
    public const CAP_POWER_BUCKET = 0x6018;
    public const ISA_X86_64 = 1;
    public const ISA_ARM64 = 2;
    public const ISA_RISCV64 = 3;
    public const ISA_WASM32 = 4;

    /** Parent id of a root node. */
    public const ROOT = 0xFFFF;
    /** Private extension bytes per node. */
    public const EXT_MAX = 256;

    // How a target component is served on a host (HwProfile::pair).
    public const NATIVE = 0;
    public const EMULATE = 1;
    public const GROUP = 2;

    private function __construct()
    {
    }

    /**
     * A fresh release key pair; the secret keys live in native memory and
     * are wiped by HwReleaseKey::close() (or its destructor).
     *
     * @param Capability $capability needs CRYPTO_SIGN
     */
    public static function keygen(Capability $capability): HwReleaseKey
    {
        $pk = Native::new("uint8_t[" . self::PK_BYTES . "]");
        $msk = new SecretKey(self::MLDSA_SK_BYTES);
        $ssk = new SecretKey(self::SLHDSA_SK_BYTES);
        try {
            Native::check(Native::ffi()->zxv_hw_keygen($capability->ptr(), $pk, $msk->pointer(), $ssk->pointer()), $capability->context);
        } catch (\Throwable $e) {
            $msk->close();
            $ssk->close();
            throw $e;
        }
        return new HwReleaseKey(Native::read($pk, self::PK_BYTES), $msk, $ssk);
    }

    /**
     * Build and sign a package: header || payload || signatures.
     *
     * @param Capability $capability needs CRYPTO_SIGN
     * @param int $kind IMG_* (for IMG_VECTORS, $fnId is the function and $version the contract)
     * @param int $svn the anti-rollback floor
     * @param ?string $targetDigest 32 bytes or null
     */
    public static function buildImage(
        Capability $capability,
        int $kind,
        int $targetClass,
        int $fnId,
        int $version,
        int $svn,
        string $payload,
        HwReleaseKey $key,
        ?string $targetDigest = null,
    ): string {
        $td = $targetDigest === null ? null : Native::bytes(Native::exact($targetDigest, "targetDigest", 32));
        $msk = SecretKey::expect($key->mldsaSecret, self::MLDSA_SK_BYTES);
        $ssk = SecretKey::expect($key->slhdsaSecret, self::SLHDSA_SK_BYTES);
        $body = Native::bytes($payload, true);
        $cap = self::HEADER_BYTES + strlen($payload) + self::SIG_BYTES;
        $len = Native::new("size_t");
        for ($attempt = 0; $attempt < 2; $attempt++) {
            $buf = Native::new("uint8_t[$cap]");
            $rc = Native::ffi()->zxv_hw_image_build(
                $capability->ptr(),
                Native::uint32($kind, "kind"),
                Native::uint32($targetClass, "targetClass"),
                $td,
                Native::uint32($fnId, "fnId"),
                Native::uint32($version, "version"),
                Native::uint32($svn, "svn"),
                $body,
                strlen($payload),
                $msk,
                $ssk,
                $buf,
                $cap,
                \FFI::addr($len),
            );
            if ($rc === Status::E_BUFFER && $len->cdata > $cap) {
                $cap = $len->cdata;
                continue;
            }
            Native::check($rc, $capability->context);
            return Native::read($buf, $len->cdata);
        }
        Native::check(Status::E_BUFFER, $capability->context);
        return "";
    }

    /**
     * The payload of an IMG_VECTORS package: each vector as op (u32 LE),
     * input length (u32 LE), input, expected length (u32 LE), expected.
     *
     * @param list<array{0:int,1:string,2:string}> $vectors [op, input, expected]
     */
    public static function vectorList(array $vectors): string
    {
        $out = "";
        foreach ($vectors as [$op, $in, $expect]) {
            $out .= pack("VV", Native::uint32($op, "op"), strlen($in)) . $in . pack("V", strlen($expect)) . $expect;
        }
        return $out;
    }

    /**
     * @internal Run a callback body for libzxv: an exception never unwinds
     * through C. A negative int the body returns is passed on as a status;
     * an exception with a status passes its status; anything else is
     * ZXV_E_IO.
     */
    public static function guard(callable $body): int
    {
        try {
            $rc = $body();
            return is_int($rc) && $rc < 0 ? $rc : Status::OK;
        } catch (\Throwable $e) {
            $s = method_exists($e, "getStatus") ? $e->getStatus() : null;
            return is_int($s) && $s < 0 ? $s : Status::E_IO;
        }
    }

    /** @internal input bytes a callback received */
    public static function input(?\FFI\CData $p, int $n): string
    {
        return $n === 0 || $p === null ? "" : \FFI::string($p, $n);
    }

    /** @internal */
    public static function int32(int $v, string $name): int
    {
        if ($v < -2147483648 || $v > 2147483647) {
            throw new ZxvUsageException(Status::E_RANGE, "$name $v is outside the int32 range");
        }
        return $v;
    }
}
