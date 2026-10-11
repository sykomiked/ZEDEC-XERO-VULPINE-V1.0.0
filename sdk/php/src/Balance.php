<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** Rail balances of an account, in exact minor units. */
final class Balance
{
    public function __construct(
        /** DEBIT 555: sum received. */
        public readonly int $debit,
        /** CREDIT 777: sum sent. */
        public readonly int $credit,
        /** EQUITY 888 = debit - credit: the spendable position. */
        public readonly int $equity,
    ) {
    }
}
