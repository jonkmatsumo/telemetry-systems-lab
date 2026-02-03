import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/screens/dataset_analytics_screen.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'package:web_ui/state/app_state.dart';
import 'package:web_ui/state/investigation_context.dart';

class _FakeTelemetryService extends TelemetryService {
  _FakeTelemetryService() : super(baseUrl: 'http://example.com');

  @override
  Future<Map<String, dynamic>> searchDatasetRecords(String datasetId,
      {int limit = 20,
      int offset = 0,
      String? region,
      String? anomalyType,
      String? isAnomaly,
      String? startTime,
      String? endTime}) async {
    return {'items': <Map<String, dynamic>>[], 'total': 0};
  }
}

void main() {
  testWidgets('RecordsBrowser shows back button and context chips', (tester) async {
    final ctx = InvestigationContext(
      datasetId: 'ds-1',
      metric: 'cpu_usage',
      useUtc: true,
      region: 'us-east',
      anomalyType: 'spike',
      isAnomaly: 'true',
      startTime: '2026-02-03T00:00:00Z',
      endTime: '2026-02-03T01:00:00Z',
    );

    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => _FakeTelemetryService()),
          ChangeNotifierProvider(create: (_) => AppState()),
        ],
        child: MaterialApp(
          home: Scaffold(
            body: RecordsBrowser(
              datasetId: 'ds-1',
              metric: 'cpu_usage',
              useUtc: true,
              contextSeed: ctx,
            ),
          ),
        ),
      ),
    );

    await tester.pump();
    expect(find.text('Back to aggregate'), findsOneWidget);
    expect(find.text('TZ: UTC'), findsOneWidget);
    expect(find.text('Region: us-east'), findsOneWidget);
  });
}
