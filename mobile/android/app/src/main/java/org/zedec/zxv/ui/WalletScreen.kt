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
    val give = state.giveBasisPoints
    val example = 10_000L // 100.00 VFV of usage
    val tithe = state.core.tithe(example)
    LazyColumn {
        item {
            ScreenTitle("Wallet")
            Panel(title = "Vino Floating Vouchers · store credit") {
                Text("—", fontSize = 32.sp, fontFamily = Zc.Mono, color = Zc.GoldSoft)
                Muted("VFV · rails DEBIT 555 / CREDIT 777 / EQUITY 888. Balance appears once the wallet ledger is bridged.")
                Muted("Every payment is confirmed on a device you hold. No interest and no late fees, ever.")
            }
            Panel(title = "Network tithe") {
                Muted("Every node gives φ percent of the compute, bandwidth and money it uses, so the network stays fast for everyone. Anything above that earns vouchers.")
                Row { Text("Base tithe ", color = Zc.Text); MonoText("φ% = 1.6180339887…%") }
                Row { Text("On 100.00 VFV of use: ", color = Zc.Text); MonoText("${Zxv.vfv(tithe)} VFV (exact, integer)") }
                Text("Your contribution: ${give / 100}.${(give % 100).toString().padStart(2, '0')}%", color = Zc.Text)
                Slider(
                    value = give.toFloat(),
                    onValueChange = { state.giveBasisPoints = it.toInt() },
                    valueRange = 162f..500f,
                )
                Muted(
                    if (give > 162) "Above the tithe by ${(give - 162) / 100}.${((give - 162) % 100).toString().padStart(2, '0')}% of usage: credited to you as vouchers."
                    else "You give exactly the base tithe. Move the slider to give more and earn vouchers."
                )
            }
            Panel(title = "Recent activity") {
                Muted("No activity on this device yet.")
            }
            Text("Statements (camt.053) are produced by the home node.", color = Zc.Muted, fontWeight = FontWeight.Normal, modifier = Modifier)
        }
    }
}
