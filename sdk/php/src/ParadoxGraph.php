<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: a complex paradox (zxv_paradox_graph): a dependency graph of up
 * to Paradox::GRAPH_MAX nodes that escalates to the owner after $stepLimit
 * passes without resolving. Node ids are 32-byte hashes of their signed
 * input (the sort key: insertion order never matters). NOT thread-safe.
 */
final class ParadoxGraph extends NativeHandle
{
    /** @param int $stepLimit >= 1 */
    public function __construct(Capability $capability, int $stepLimit)
    {
        $out = Native::new("zxv_paradox_graph*");
        Native::check(Native::ffi15()->zxv_paradox_graph_create($capability->ptr(), Native::uint32($stepLimit, "stepLimit"), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_paradox_graph_destroy");
    }

    /**
     * Add a node before the first pass.
     * @param int $mag tension magnitude (uint32)
     * @param int $dir tension direction: -1, 0 or +1
     * @throws Exception\ZxvUsageException ZXV_E_DUPLICATE (id present)
     * @throws Exception\ZxvRefusedException ZXV_E_STATE (after the first pass), ZXV_E_CAPACITY
     */
    public function addNode(string $id, int $mag, int $dir, ?ParadoxClassification $pair = null, string|int|null $kind = null, Hexagram|int|null $tag = null): self
    {
        [$k, $t] = Paradox::pair($pair, $kind, $tag);
        Native::check(Native::ffi15()->zxv_paradox_graph_add_node($this->ptr(), Paradox::id($id), $k, $t, Native::uint32($mag, "mag"), Native::int32($dir, "dir")), $this->context);
        return $this;
    }

    /** Node $id depends on node $dep (which must exist by the first pass). */
    public function addDependency(string $id, string $dep): self
    {
        Native::check(Native::ffi15()->zxv_paradox_graph_add_dep($this->ptr(), Paradox::id($id), Paradox::id($dep, "dep")), $this->context);
        return $this;
    }

    /** Queue new evidence for a node, applied at the start of the next pass ("worst wins" within a batch). */
    public function update(string $id, int $mag, int $dir, ?ParadoxClassification $pair = null, string|int|null $kind = null, Hexagram|int|null $tag = null): self
    {
        [$k, $t] = Paradox::pair($pair, $kind, $tag);
        Native::check(Native::ffi15()->zxv_paradox_graph_update($this->ptr(), Paradox::id($id), $k, $t, Native::uint32($mag, "mag"), Native::int32($dir, "dir")), $this->context);
        return $this;
    }

    /** The node's state (a Paradox::NODE_STATES name; "open" before the first pass). */
    public function state(string $id): string|int
    {
        [$o, $oP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_paradox_graph_state($this->ptr(), Paradox::id($id), $oP), $this->context);
        return Paradox::name(Paradox::NODE_STATES, $o->cdata);
    }

    /** @return array{id:string, kind:string|int, state:string|int} node $index in id order (after the first pass) */
    public function node(int $index): array
    {
        $id = Native::new("uint8_t[32]");
        [$k, $kP] = Native::out("int32_t");
        [$s, $sP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_paradox_graph_node_at($this->ptr(), Native::uint32($index, "index"), $id, $kP, $sP), $this->context);
        return ["id" => Native::read($id, 32), "kind" => Paradox::name(Paradox::KINDS, $k->cdata), "state" => Paradox::name(Paradox::NODE_STATES, $s->cdata)];
    }

    /** @return list<array{id:string, kind:string|int, state:string|int}> */
    public function nodes(): array
    {
        $out = [];
        for ($i = 0, $n = $this->info()["nodes"]; $i < $n; $i++) {
            $out[] = $this->node($i);
        }
        return $out;
    }

    /**
     * Node count, passes run, net tension of the open nodes after the last
     * pass, whether it escalated, and the SHA-256 chain over every pass.
     * @return array{nodes:int, passes:int, totalMag:int, totalDir:int, escalated:bool, chain:string}
     */
    public function info(): array
    {
        [$n, $nP] = Native::out("uint32_t");
        [$p, $pP] = Native::out("uint32_t");
        [$m, $mP] = Native::out("uint32_t");
        [$d, $dP] = Native::out("int32_t");
        [$e, $eP] = Native::out("int32_t");
        $chain = Native::new("uint8_t[32]");
        Native::check(Native::ffi15()->zxv_paradox_graph_info($this->ptr(), $nP, $pP, $mP, $dP, $eP, $chain), $this->context);
        return ["nodes" => $n->cdata, "passes" => $p->cdata, "totalMag" => $m->cdata, "totalDir" => $d->cdata, "escalated" => $e->cdata !== 0, "chain" => Native::read($chain, 32)];
    }
}
