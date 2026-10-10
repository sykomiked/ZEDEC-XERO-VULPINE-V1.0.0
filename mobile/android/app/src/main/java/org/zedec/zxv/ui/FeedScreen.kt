// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

private data class Post(val who: String, val where: String, val signal: String, val body: String)

/** Sample content until the social module (kernel/src/social) is bridged. */
private val samplePosts = listOf(
    Post("Mira Kovač", "Open Hardware Guild · 18 min", "new signal 0.82",
        "Measured our Leech-lattice decoder: 20 µs per 24 weights. Notes are in the group drive."),
    Post("Tomás Oyelaran", "Coastal Growers Syndicate · 1 h", "new signal 0.64",
        "Our treasury now needs 3 of 5 signatures for any spend over 500 VFV."),
)
private val groups = listOf(
    "Open Hardware Guild" to "Group · 214 members · you are admin",
    "Neighbourhood Compute Co-op" to "Group · 38 members",
    "Coastal Growers Syndicate" to "Syndicate · 12 organisations · treasury 3-of-5",
)

@Composable
fun FeedScreen() {
    var following by remember { mutableStateOf(false) }
    LazyColumn {
        item {
            ScreenTitle("Feed and groups")
            Row {
                OutlinedButton(onClick = { following = false }) { Text(if (!following) "• New to you" else "New to you") }
                Spacer(Modifier.width(8.dp))
                OutlinedButton(onClick = { following = true }) { Text(if (following) "• Following" else "Following") }
            }
            Muted("Ranked by Interaction Surplus: posts that add something you have not seen rise; repeats sink.")
            Muted("Sample posts: the social module is not bridged to the phone yet.")
        }
        items(samplePosts) { p ->
            Panel {
                Text(p.who, fontWeight = FontWeight.SemiBold)
                Muted("${p.where} · ${p.signal}")
                Text(p.body, modifier = Modifier)
                Row {
                    TextButton(onClick = {}) { Text("Reply") }
                    TextButton(onClick = {}) { Text("Tip vouchers") }
                }
            }
        }
        item { Panel(title = "Your groups") { groups.forEach { (n, d) -> Text(n); Muted(d) } } }
    }
}
