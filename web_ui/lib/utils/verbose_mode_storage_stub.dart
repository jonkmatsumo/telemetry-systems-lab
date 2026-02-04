class VerboseModeStorageImpl {
  static bool _value = false;

  static bool read() => _value;

  static void write(bool value) {
    _value = value;
  }
}
