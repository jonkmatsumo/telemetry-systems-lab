import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/services/telemetry_service.dart';

void main() {
  test('ResponseMeta parses new truncation fields', () {
    final json = {
      'limit': 100,
      'returned': 10,
      'truncated': true,
      'total_distinct': 500,
      'reason': 'max_bins_cap',
      'bins_requested': 100,
      'bins_returned': 10,
    };
    
    final meta = ResponseMeta.fromJson(json);
    expect(meta.limit, 100);
    expect(meta.returned, 10);
    expect(meta.truncated, true);
    expect(meta.totalDistinct, 500);
    expect(meta.reason, 'max_bins_cap');
    expect(meta.binsRequested, 100);
    expect(meta.binsReturned, 10);
  });
}
