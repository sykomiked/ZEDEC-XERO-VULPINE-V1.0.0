// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// The C core (kernel/src/devmesh + capmkt + pq crypto) is built by the same
// script used for iOS and Linux, then linked into libzxvcore_jni.so by CMake.
val zxvCoreDir = layout.buildDirectory.dir("zxvcore").get().asFile
val buildZxvCore by tasks.registering(Exec::class) {
    group = "build"
    description = "Build libzxvcore.a for arm64-v8a with mobile/core/build_mobile_core.sh"
    workingDir = rootProject.projectDir
    commandLine("../core/build_mobile_core.sh", "android-arm64", zxvCoreDir.absolutePath)
    environment("ANDROID_NDK_HOME", android.ndkDirectory.absolutePath)
    inputs.dir("../../kernel/src")
    inputs.dir("../core")
    outputs.dir(zxvCoreDir)
}

android {
    namespace = "org.zedec.zxv"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "org.zedec.zxv"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake { arguments += listOf("-DZXV_CORE_DIR=${zxvCoreDir.absolutePath}") }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
    packaging { jniLibs { useLegacyPackaging = false } }
}

tasks.configureEach {
    if (name.startsWith("configureCMake") || name.startsWith("buildCMake")) dependsOn(buildZxvCore)
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("com.google.zxing:core:3.5.3")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
