// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Kotlin/JVM build of the SDK, and the host tests that run the Kotlin code
// and the JNI shim against libzxv and sdk/conformance/vectors.json.
//
//   make -C ../../libzxv all fixtures      (once)
//   gradle -p sdk/mobile/kotlin :zxv:test
//
// Properties: -Pzxv.libDir=<dir with libzxv.so/.dylib> (default sdk/libzxv/build)
//             -Pzxv.testJavaHome=<JDK> runs the tests on another JDK (e.g. 25)
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm")
    `java-library`
}

group = "org.zedec"
version = "1.6.0"

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
        allWarningsAsErrors.set(true)
    }
}

dependencies {
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
    testImplementation(kotlin("test"))
    testImplementation("org.junit.jupiter:junit-jupiter:5.11.4")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher:1.11.4")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.10.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.9.0")
}

val sdkRoot = rootDir.resolve("../..").canonicalFile
val libDir = providers.gradleProperty("zxv.libDir").map { file(it) }.orElse(provider { sdkRoot.resolve("libzxv/build") })
val jniBuild = layout.buildDirectory.dir("jni")
val isMac = System.getProperty("os.name").lowercase().contains("mac")
val libExt = if (isMac) "dylib" else "so"

val configureJni by tasks.registering(Exec::class) {
    description = "Configures the JNI shim (jni/CMakeLists.txt) for the host"
    inputs.file(rootDir.resolve("jni/CMakeLists.txt"))
    outputs.dir(jniBuild)
    doFirst {
        val lib = libDir.get().resolve("libzxv.$libExt")
        check(lib.isFile) { "libzxv not found at $lib: run make -C sdk/libzxv all fixtures" }
    }
    commandLine(
        "cmake", "-S", rootDir.resolve("jni").path, "-B", jniBuild.get().asFile.path,
        "-DCMAKE_BUILD_TYPE=Release", "-DZXV_LIB_DIR=${libDir.get().path}",
    )
}

val buildJni by tasks.registering(Exec::class) {
    description = "Builds libzxvjni for the host JVM"
    dependsOn(configureJni)
    inputs.file(rootDir.resolve("jni/zxv_jni.c"))
    inputs.file(rootDir.resolve("jni/zxv_jni15.c"))
    inputs.file(rootDir.resolve("jni/zxv_jni16.c"))
    inputs.file(rootDir.resolve("jni/zxvj_common.h"))
    inputs.file(sdkRoot.resolve("libzxv/include/zxv_api.h"))
    outputs.file(jniBuild.map { it.file("libzxvjni.$libExt") })
    commandLine("cmake", "--build", jniBuild.get().asFile.path)
}

tasks.test {
    dependsOn(buildJni)
    useJUnitPlatform()
    // System.load is a restricted method on JDK 22+; JNI needs native access granted.
    jvmArgs("--enable-native-access=ALL-UNNAMED")
    providers.gradleProperty("zxv.testJavaHome").orNull?.let { home ->
        executable = file(home).resolve("bin/java").path
    }
    systemProperty("zxv.library", libDir.get().resolve("libzxv.$libExt").path)
    systemProperty("zxv.jni.library", jniBuild.get().file("libzxvjni.$libExt").asFile.path)
    systemProperty("zxv.vectors", sdkRoot.resolve("conformance/vectors.json").path)
    systemProperty("zxv.jniSource", rootDir.resolve("jni/zxv_jni.c").path)
    systemProperty("zxv.jni15Source", rootDir.resolve("jni/zxv_jni15.c").path)
    systemProperty("zxv.jni16Source", rootDir.resolve("jni/zxv_jni16.c").path)
    testLogging {
        events("failed")
        exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL
    }
}
