import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart';
import '../baresdk.dart';

/// Loads the native library for the current platform.
DynamicLibrary loadLib() {
  if (Platform.isAndroid) return DynamicLibrary.open('libbaresdk.so');
  if (Platform.isIOS)     return DynamicLibrary.process();
  if (Platform.isMacOS)   return DynamicLibrary.open('libbaresdk.dylib');
  if (Platform.isWindows) return DynamicLibrary.open('baresdk.dll');
  // Linux
  return DynamicLibrary.open('libbaresdk.so');
}

late final BareSDKBindings _bindings = BareSDKBindings(loadLib());

BareSDKBindings get nativeBindings => _bindings;

/// Internal: allocate a C string and free it after use.
Pointer<Char> _cstr(String s) => s.toNativeUtf8().cast<Char>();
