import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/utils/time_buckets.dart';

void main() {
  test('bucketEndFromIso adds bucket seconds', () {
    final end = bucketEndFromIso('2026-02-03T00:00:00Z', 3600);
    expect(end, isNotNull);
    expect(end!.toUtc().toIso8601String(), '2026-02-03T01:00:00.000Z');
  });

  test('formatBucketLabel uses utc when requested', () {
    final dt = DateTime.parse('2026-02-03T05:30:00Z');
    final label = formatBucketLabel(dt, useUtc: true);
    expect(label, '05:30');
  });
}
