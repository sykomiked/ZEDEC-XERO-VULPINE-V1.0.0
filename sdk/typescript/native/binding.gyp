# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# The Node-API addon of @zedec/zxv. It compiles against zxv_api.h only and
# opens libzxv at run time (dlopen / LoadLibrary), so one addon binary works
# with any ABI 1.x library the developer points it at.
{
  "targets": [
    {
      "target_name": "zxv_addon",
      "sources": ["zxv_addon.cc"],
      "include_dirs": [
        "<!(node -p \"require('node-addon-api').include_dir\")",
        "include",
        "../../libzxv/include"
      ],
      "defines": ["NAPI_VERSION=8", "NAPI_CPP_EXCEPTIONS", "NODE_ADDON_API_DISABLE_DEPRECATED"],
      "cflags_cc": ["-std=c++17", "-fexceptions", "-Wall", "-Wextra"],
      "cflags_cc!": ["-fno-exceptions"],
      "libraries": [],
      "conditions": [
        ["OS=='linux'", {"libraries": ["-ldl"]}],
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
            "MACOSX_DEPLOYMENT_TARGET": "11.0"
          }
        }],
        ["OS=='win'", {
          "msvs_settings": {"VCCLCompilerTool": {"ExceptionHandling": 1, "AdditionalOptions": ["/std:c++17"]}}
        }]
      ]
    }
  ]
}
