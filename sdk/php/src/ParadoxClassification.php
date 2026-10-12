<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: what ParadoxBook::classify returned: the kind (a Paradox::KINDS
 * name), the tag with the kind's polarity and the registry index that
 * decided it (null if none). Pass it to an operator as $pair.
 */
final class ParadoxClassification
{
    public function __construct(
        public readonly string|int $kind,
        public readonly Hexagram $tag,
        public readonly ?int $registryIndex,
    ) {
    }

    /** @return array{kind:string|int, tag:int, registryIndex:?int} */
    public function toArray(): array
    {
        return ["kind" => $this->kind, "tag" => $this->tag->tag, "registryIndex" => $this->registryIndex];
    }
}
