import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart';
import '../baresdk.dart';

/// Loads the native library for the current platform.
/// Pass [libPath] to override the default search-path lookup with an
/// absolute path, e.g. '/opt/myapp/baresdk.so'.
DynamicLibrary loadLib({String? libPath}) {
  if (Platform.isIOS) return DynamicLibrary.process();
  if (libPath != null) return DynamicLibrary.open(libPath);
  if (Platform.isAndroid) return DynamicLibrary.open('baresdk.so');
  if (Platform.isMacOS)   return DynamicLibrary.open('baresdk.dylib');
  if (Platform.isWindows) return DynamicLibrary.open('baresdk.dll');
  // Linux
  return DynamicLibrary.open('baresdk.so');
}

String? _libPath;

/// Set before the first [nativeBindings] access to load from a custom path.
void setLibPath(String path) => _libPath = path;

late final BareSDKBindings _bindings = BareSDKBindings(loadLib(libPath: _libPath));

BareSDKBindings get nativeBindings => _bindings;

/// Internal: allocate a C string and free it after use.
Pointer<Char> _cstr(String s) => s.toNativeUtf8().cast<Char>();
