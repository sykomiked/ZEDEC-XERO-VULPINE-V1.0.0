# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# R8/ProGuard rules shipped inside the AAR: JNI looks methods up by name.
-keep class org.zedec.zxv.mobile.internal.Jni { native <methods>; }
-keepclasseswithmembernames class * { native <methods>; }
