import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/utils/freshness.dart';

void main() {
  test('maxFreshnessDelta returns null when insufficient samples', () {
    final delta = maxFreshnessDelta(const [WidgetFreshness(requestEnd: null)]);
    expect(delta, isNull);
  });

  test('shouldShowFreshnessBanner when delta exceeds threshold', () {
    final now = DateTime.utc(2026, 2, 3, 12, 0, 0);
    final samples = [
      WidgetFreshness(requestEnd: now),
      WidgetFreshness(requestEnd: now.add(const Duration(seconds: 90))),
    ];
    expect(shouldShowFreshnessBanner(samples), isTrue);
  });

  test('hasMixedRefreshMode detects cached and forced mix', () {
    final samples = [
      const WidgetFreshness(forceRefresh: true, requestEnd: null),
      const WidgetFreshness(forceRefresh: false, requestEnd: null),
    ];
    expect(hasMixedRefreshMode(samples), isTrue);
  });
}
