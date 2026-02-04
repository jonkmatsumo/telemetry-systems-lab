// ignore_for_file: avoid_web_libraries_in_flutter, deprecated_member_use

import 'dart:html' as html;

class VerboseModeStorageImpl {
  static const String _key = 'tads.verboseMode';

  static bool read() {
    return html.window.localStorage[_key] == '1';
  }

  static void write(bool value) {
    if (value) {
      html.window.localStorage[_key] = '1';
    } else {
      html.window.localStorage.remove(_key);
    }
  }
}
