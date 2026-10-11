// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// :zxv-android, the Android library (.aar): the same Kotlin sources as :zxv,
// plus libzxvjni.so built by the NDK from ../jni, plus a prebuilt libzxv.so
// per ABI. Included only with -Pzxv.android=true.
//
//   scripts/build-libzxv-android.sh             # libzxv.so for each ABI (NDK)
//   gradle -p sdk/mobile/kotlin -Pzxv.android=true :zxv-android:assembleRelease
//
// Properties: -Pzxv.abis=arm64-v8a,x86_64 (default both)
//             -Pzxv.androidLibs=<dir>  holds <abi>/libzxv.so
//                                      (default build/libzxv-android)
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.library")
    kotlin("android")
}

val abis = providers.gradleProperty("zxv.abis").orNull?.split(",")?.map { it.trim() }
    ?: listOf("arm64-v8a", "x86_64")
val libsRoot = providers.gradleProperty("zxv.androidLibs").map { file(it) }
    .orElse(provider { rootDir.resolve("build/libzxv-android") }).get()

android {
    namespace = "org.zedec.zxv.mobile"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        // 24: ConcurrentHashMap.newKeySet and the JNI shim's needs. libzxv is
        // built with arc4random_buf, which bionic has at every API level.
        minSdk = 24
        ndk { abiFilters += abis }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DZXV_ANDROID_LIBS_ROOT=${libsRoot.path}", "-DANDROID_STL=none")
            }
        }
        consumerProguardFiles("consumer-rules.pro")
    }

    externalNativeBuild {
        cmake {
            path = rootDir.resolve("jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].apply {
        java.srcDirs(rootDir.resolve("zxv/src/main/kotlin"))
        manifest.srcFile("src/main/AndroidManifest.xml")
        // libzxv.so per ABI; libzxvjni.so comes from externalNativeBuild.
        jniLibs.srcDirs(libsRoot)
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
}

dependencies {
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
}

tasks.named("preBuild") {
    doFirst {
        for (abi in abis) {
            val so = libsRoot.resolve("$abi/libzxv.so")
            check(so.isFile) { "missing $so: run scripts/build-libzxv-android.sh first" }
        }
    }
}
