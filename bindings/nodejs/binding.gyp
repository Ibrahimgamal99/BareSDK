{
  "targets": [{
    "target_name": "baresdk",
    "sources": [ "src/baresdk_addon.cpp" ],
    "include_dirs": [
      "<!@(node -p \"require('node-addon-api').include\")",
      "../../include"
    ],
    "cflags_cc": [ "-std=c++17", "-fexceptions" ],
    "defines": [ "NAPI_CPP_EXCEPTIONS" ],
    "conditions": [
      ["OS=='linux'", {
        "libraries": [
          "<!@(node -p \"(process.env.BARESDK_DIST_DIR || require('path').resolve(__dirname, '../../dist/linux/x86_64')) + '/baresdk.so'\")"
        ],
        "ldflags": [
          "<!@(node -p \"'-Wl,-rpath,' + (process.env.BARESDK_DIST_DIR || require('path').resolve(__dirname, '../../dist/linux/x86_64'))\")"
        ]
      }],
      ["OS=='mac'", {
        "libraries": [
          "<!@(node -p \"(process.env.BARESDK_DIST_DIR || require('path').resolve(__dirname, '../../dist/macos/universal')) + '/baresdk.dylib'\")"
        ],
        "xcode_settings": {
          "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
          "CLANG_CXX_LANGUAGE_STANDARD": "c++17"
        }
      }],
      ["OS=='win'", {
        "libraries": [
          "<!@(node -p \"(process.env.BARESDK_DIST_DIR || require('path').resolve(__dirname, '../../dist/windows/x64')) + '/baresdk.lib'\")",
          "ws2_32.lib", "iphlpapi.lib", "crypt32.lib"
        ]
      }]
    ]
  }]
}
