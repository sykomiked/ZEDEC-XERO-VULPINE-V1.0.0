// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// quickstart.cpp — the README quickstart, runnable: exact money, a narrowed
// capability, four-valued logic, a budget, and the fail-closed path.
#include <zxv.hpp>

#include <iostream>

int main()
{
    zxv::Context ctx;                                      // one platform instance
    zxv::Capability root = ctx.root_capability();          // every bit, attested TRUE
    ctx.enable_currency(root, "NGN");                     // needs caps::ledger_post
    zxv::Ledger ledger(root);
    auto bank = ledger.open_account("bank", "NGN", zxv::AccountKind::issuer);
    auto ada = ledger.open_account("ada", "NGN");
    ledger.post(bank, ada, 125075, "NGN", "salary-001");   // 1,250.75 NGN in kobo
    std::cout << "ada equity: " << ledger.balance(ada).equity << " kobo\n";

    // Exact rationals: 0.1 + 0.2 is 3/10, and a double's real value is visible.
    auto sum = zxv::Rational::parse("0.1") + zxv::Rational::parse("0.2");
    std::cout << "0.1 + 0.2 = " << sum << "; the double 0.1 is "
              << zxv::Rational::from_double_exact(0.1).num() << "/" << zxv::Rational::from_double_exact(0.1).den() << "\n";

    // Least privilege: a read-only capability cannot post.
    zxv::Ledger reader(root.attenuate(zxv::caps::ledger_read));
    try {
        reader.post(bank, ada, 1, "NGN", "nope");
    } catch (const zxv::CapabilityDenied& e) {
        std::cout << "refused as expected: " << e.status_name() << "\n";
    }

    // Four-valued logic: a contradiction is held, never collapsed silently.
    zxv::Truth t = zxv::truth_from_evidence(3, 1);
    std::cout << "evidence 3 for, 1 against: " << t << " (certainly true? "
              << std::boolalpha << zxv::is_certainly_true(t) << ")\n";

    // A token budget with an exact limit.
    zxv::Budgets budgets(root);
    auto tokens = budgets.create("tokens", zxv::Resource::tokens, 100);
    tokens.consume(zxv::Rational::of(250, 4));
    std::cout << "tokens left: " << tokens.status().remaining << "\n";

    // Fail closed: a halted context refuses everything with FatalError.
    ctx.halt("operator kill switch");
    try {
        ledger.check();
    } catch (const zxv::FatalError& e) {
        std::cout << "fatal (by design): " << e.what() << "\n";
    }
    return 0;
}
