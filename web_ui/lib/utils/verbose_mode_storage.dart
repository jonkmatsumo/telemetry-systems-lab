import 'verbose_mode_storage_stub.dart'
    if (dart.library.html) 'verbose_mode_storage_web.dart';

class VerboseModeStorage {
  static bool read() => VerboseModeStorageImpl.read();
  static void write(bool value) => VerboseModeStorageImpl.write(value);
}
