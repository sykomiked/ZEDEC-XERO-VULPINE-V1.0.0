// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The ZXV Kotlin SDK: one Kotlin source tree, two builds.
//   :zxv          Kotlin/JVM library + host tests (JNI shim built with CMake)
//   :zxv-android  Android library (.aar), only with -Pzxv.android=true,
//                 so a host-only build never needs the Android SDK or AGP.
pluginManagement {
    repositories {
        gradlePluginPortal()
        google()
        mavenCentral()
    }
    plugins {
        kotlin("jvm") version "2.2.21"
        kotlin("android") version "2.2.21"
        id("com.android.library") version "8.13.0"
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "zxv-kotlin"
include(":zxv")

if (providers.gradleProperty("zxv.android").orNull == "true") {
    include(":zxv-android")
    project(":zxv-android").projectDir = file("android")
}
