import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/utils/freshness.dart';

void main() {
  group('Freshness Logic', () {
    test('maxFreshnessDelta returns correct diff', () {
      final now = DateTime.now();
      final items = [
        WidgetFreshness(serverTime: now),
        WidgetFreshness(serverTime: now.subtract(const Duration(seconds: 30))),
        WidgetFreshness(serverTime: now.subtract(const Duration(seconds: 10))),
      ];
      final delta = maxFreshnessDelta(items);
      expect(delta!.inSeconds, 30);
    });

    test('shouldShowFreshnessBanner respects threshold', () {
      final now = DateTime.now();
      final items = [
        WidgetFreshness(serverTime: now),
        WidgetFreshness(serverTime: now.subtract(const Duration(seconds: 70))),
      ];
      expect(shouldShowFreshnessBanner(items, threshold: const Duration(seconds: 60)), true);
      expect(shouldShowFreshnessBanner(items, threshold: const Duration(seconds: 80)), false);
    });

    test('shouldShowFreshnessBanner ignores single item', () {
      final items = [WidgetFreshness(serverTime: DateTime.now())];
      expect(shouldShowFreshnessBanner(items), false);
    });

    test('hasMixedRefreshMode detects mixed states', () {
      final mixed = [
        const WidgetFreshness(forceRefresh: true),
        const WidgetFreshness(forceRefresh: false),
      ];
      expect(hasMixedRefreshMode(mixed), true);

      final allForced = [
        const WidgetFreshness(forceRefresh: true),
        const WidgetFreshness(forceRefresh: true),
      ];
      expect(hasMixedRefreshMode(allForced), false);
    });
  });
}