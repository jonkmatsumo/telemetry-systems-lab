import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/utils/time_buckets.dart';

void main() {
  test('formatBucketLabel respects UTC/Local', () {
    final dt = DateTime.parse('2023-01-01T12:00:00Z'); // 12:00 UTC
    
    // UTC
    expect(formatBucketLabel(dt, useUtc: true), '12:00');

    // Local (Test environment might be UTC, so let's mock or just check offset behavior)
    // Actually, DateTime.toLocal() depends on system TZ.
    // If system is UTC, it's 12:00. If PST (-8), it's 04:00.
    // We can't easily assert exact string without knowing system TZ.
    // But we can check that it calls toLocal().
    
    final local = dt.toLocal();
    final hh = local.hour.toString().padLeft(2, '0');
    final mm = local.minute.toString().padLeft(2, '0');
    expect(formatBucketLabel(dt, useUtc: false), '$hh:$mm');
  });
}