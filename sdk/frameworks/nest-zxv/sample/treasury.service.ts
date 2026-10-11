// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Injectable } from '@nestjs/common';
import { AccountKind, type Balance, type Capability, Ledger } from '@zedec/zxv';

import { InjectCapability } from '../src/index.js';

/** A bank account and one customer account in NGN: posted with `teller`, read with `auditor`. */
@Injectable()
export class TreasuryService {
  readonly #teller: Ledger;
  readonly #auditor: Ledger;
  readonly #bank: number;
  readonly #customer: number;

  constructor(@InjectCapability('teller') teller: Capability, @InjectCapability('auditor') auditor: Capability) {
    this.#teller = Ledger.of(teller);
    this.#auditor = Ledger.of(auditor);
    this.#bank = this.#teller.openAccount('bank', 'NGN', AccountKind.ISSUER);
    this.#customer = this.#teller.openAccount('ada', 'NGN', AccountKind.HOLDER);
  }

  /** Pays the customer from the bank. Returns the journal entry id. */
  payCustomer(amountMinor: bigint, reference: string): bigint {
    return this.#teller.post(this.#bank, this.#customer, amountMinor, 'NGN', reference);
  }

  /** Tries to refund through the read-only auditor: always CapabilityDeniedError (403). */
  refundAsAuditor(amountMinor: bigint, reference: string): bigint {
    return this.#auditor.post(this.#customer, this.#bank, amountMinor, 'NGN', reference);
  }

  /** The customer's balance, read with the auditor capability. */
  customerBalance(): Balance {
    return this.#auditor.balance(this.#customer);
  }
}
