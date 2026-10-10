// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv

import android.app.ActivityManager
import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.BatteryManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import org.zedec.zxv.core.LanTransport
import org.zedec.zxv.core.NativeCore
import org.zedec.zxv.core.SeedStore
import org.zedec.zxv.model.AppState
import org.zedec.zxv.model.Zxv
import org.zedec.zxv.ui.ZxvApp

class MainActivity : ComponentActivity() {
    private lateinit var core: NativeCore
    private lateinit var lan: LanTransport
    private lateinit var state: AppState
    private val main = Handler(Looper.getMainLooper())
    private val now = { System.currentTimeMillis() }

    private val ticker = object : Runnable {
        override fun run() {
            core.tick(now())
            main.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        lan = LanTransport { frame -> core.receive(frame, now()) }
        core = NativeCore { to, frame -> lan.send(to, frame) }
        val seed = SeedStore.load(this)
        core.start(seed, Build.MODEL.take(31), Zxv.ROLE_STANDALONE, Zxv.FLAG_HELD)
        seed.fill(0)
        // Phone-only use: a mesh of one. Pairing a home computer later adds it.
        // (The roster lives in memory in this build; persisting it is listed as not done.)
        core.createMesh(now())
        state = AppState(core, now)
        state.setNetwork(networkKind(), ramMb(), 400, battery(), charging())
        lan.start()
        main.post(ticker)
        setContent { ZxvApp(state) }
    }

    override fun onDestroy() {
        main.removeCallbacks(ticker)
        lan.stop()
        super.onDestroy()
    }

    private fun ramMb(): Int {
        val mi = ActivityManager.MemoryInfo()
        (getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).getMemoryInfo(mi)
        return (mi.totalMem / (1024 * 1024)).toInt()
    }

    private fun battery(): Int =
        (getSystemService(Context.BATTERY_SERVICE) as BatteryManager).getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)

    private fun charging(): Boolean = (getSystemService(Context.BATTERY_SERVICE) as BatteryManager).isCharging

    private fun networkKind(): Int {
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val caps = cm.getNetworkCapabilities(cm.activeNetwork) ?: return Zxv.NET_OFFLINE
        if (!caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) return Zxv.NET_LAN
        return if (caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)) Zxv.NET_UNMETERED else Zxv.NET_METERED
    }
}
