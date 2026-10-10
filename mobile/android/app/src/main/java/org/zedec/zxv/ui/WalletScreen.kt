// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import org.zedec.zxv.model.AppState
import org.zedec.zxv.model.Zxv

@Composable
fun WalletScreen(state: AppState) {
    val give = state.giveTenMillionths
    val example = 10_000L // 100.00 VFV of usage
    val fee = state.core.assureFee(example)
    fun pct(v: Int) = "${v / 100_000}.${(v % 100_000).toString().padStart(5, '0')}%"
    LazyColumn {
        item {
            ScreenTitle("Wallet")
            Panel(title = "Vino Floating Vouchers · store credit") {
                Text("—", fontSize = 32.sp, fontFamily = Zc.Mono, color = Zc.GoldSoft)
                Muted("VFV · rails DEBIT 555 / CREDIT 777 / EQUITY 888. Balance appears once the wallet ledger is bridged.")
                Muted("Every payment is confirmed on a device you hold. No interest and no late fees, ever.")
            }
            Panel(title = "Network assurance fee") {
                Muted("Every payment carries a 0.08889% assurance fee. Half backs the Vino reserve floor; the rest funds the V-Bill dividend pool, node bounties and regenerative capital. Anything above it earns vouchers.")
                Row { Text("Base fee ", color = Zc.Text); MonoText("0.08889% = 8889 / 10,000,000") }
                Row { Text("On 100.00 VFV of use: ", color = Zc.Text); MonoText("${Zxv.vfv(fee)} VFV (exact, integer; sub-unit remainders carry to the next payment)") }
                Text("Your contribution: ${pct(give)}", color = Zc.Text)
                Slider(
                    value = give.toFloat(),
                    onValueChange = { state.giveTenMillionths = it.toInt() },
                    valueRange = 8889f..500000f,
                )
                Muted(
                    if (give > 8889) "Above the fee by ${pct(give - 8889)} of usage: credited to you as vouchers."
                    else "You give exactly the base fee. Move the slider to give more and earn vouchers."
                )
            }
            Panel(title = "Recent activity") {
                Muted("No activity on this device yet.")
            }
            Text("Statements (camt.053) are produced by the home node.", color = Zc.Muted, fontWeight = FontWeight.Normal, modifier = Modifier)
        }
    }
}
